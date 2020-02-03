// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "DataBlockAggregator.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

DataBlockAggregator::DataBlockAggregator(
    AliceO2::Common::Fifo<DataSetReference> *v_output, std::string name) {
  output = v_output;
  aggregateThread = std::make_unique<Thread>(
      DataBlockAggregator::threadCallback, this, name, 1000);
  isIncompletePending = 0;
}

DataBlockAggregator::~DataBlockAggregator() {
  // todo: flush out FIFOs ?
}

int DataBlockAggregator::addInput(
    std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> input) {
  // inputs.push_back(input);
  inputs.push_back(input);
  slicers.push_back(DataBlockSlicer());
  return 0;
}

Thread::CallbackResult DataBlockAggregator::threadCallback(void *arg) {
  DataBlockAggregator *dPtr = (DataBlockAggregator *)arg;
  if (dPtr == NULL) {
    return Thread::CallbackResult::Error;
  }

  if (dPtr->output->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  return dPtr->executeCallback();
}

void DataBlockAggregator::start() {
  for (unsigned int ix = 0; ix < inputs.size(); ix++) {
    slicers[ix].slicerId = ix;
  }
  doFlush = 0;
  timeNow.reset();
  aggregateThread->start();
}

void DataBlockAggregator::stop(int waitStop) {
  doFlush = 0;
  aggregateThread->stop();
  if (waitStop) {
    aggregateThread->join();
  }
  theLog.log("Aggregator processed %llu blocks", totalBlocksIn);
  for (unsigned int i = 0; i < inputs.size(); i++) {

    //    printf("aggregator input %d: in=%llu
    //    out=%llu\n",i,inputs[i]->getNumberIn(),inputs[i]->getNumberOut());
    //    printf("Aggregator FIFO in %d clear: %d
    //    items\n",i,inputs[i]->getNumberOfUsedSlots());

    inputs[i]->clear();
  }
  //  printf("Aggregator FIFO out after clear: %d
  //  items\n",output->getNumberOfUsedSlots());
  /* todo: do we really need to clear? should be automatic */

  DataSetReference bc = nullptr;
  while (!output->pop(bc)) {
    bc->clear();
  }
  output->clear();
}

Thread::CallbackResult DataBlockAggregator::executeCallback() {

  if (output->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  unsigned int nInputs = inputs.size();
  unsigned int nBlocksIn = 0;
  unsigned int nSlicesOut = 0;

  // get time once per iteration
  double now = timeNow.getTime();

  for (unsigned int ix = 0; ix < nInputs; ix++) {
    int i = (ix + nextIndex) % nInputs;

    if (disableSlicing) {
      // no slicing... pass through
      if (output->isFull()) {
        return Thread::CallbackResult::Idle;
      }
      if (inputs[i]->isEmpty()) {
        continue;
      }
      DataBlockContainerReference b = nullptr;
      inputs[i]->pop(b);
      nBlocksIn++;
      totalBlocksIn++;
      DataSetReference bcv = nullptr;
      try {
        bcv = std::make_shared<DataSet>();
      } catch (...) {
        return Thread::CallbackResult::Error;
      }
      bcv->push_back(b);
      output->push(bcv);
      nSlicesOut++;
      continue;
    }

    const int maxLoop = 1024;

    // populate slices
    for (int j = 0; j < maxLoop; j++) {
      if (inputs[i]->isEmpty()) {
        break;
      }
      DataBlockContainerReference b = nullptr;
      inputs[i]->pop(b);
      nBlocksIn++;
      totalBlocksIn++;
      // printf("Got block %d from dev %d eq %d link %d tf
      // %d\n",(int)(b->getData()->header.blockId),
      // i,(int)(b->getData()->header.equipmentId),
      // (int)(b->getData()->header.linkId),
      // (int)(b->getData()->header.timeframeId));
      slicers[i].appendBlock(b, now);
    }

    // close incomplete slices on timeout
    if (cfgSliceTimeout) {
      slicers[i].completeSliceOnTimeout(now - cfgSliceTimeout);
    }

    // retrieve completed slices
    for (int j = 0; j < maxLoop; j++) {
      if (output->isFull()) {
        return Thread::CallbackResult::Idle;
      }
      bool includeIncomplete = 0;
      if ((doFlush) && (inputs[i]->isEmpty())) {
        includeIncomplete = 1;
      }
      DataSetReference bcv = slicers[i].getSlice(includeIncomplete);
      if (bcv == nullptr) {
        break;
      }
      output->push(bcv);
      nSlicesOut++;
      nextIndex = i + 1;
      // printf("Pushed STF : %d chunks\n",(int)bcv->size());
    }
  }

  if ((nBlocksIn == 0) && (nSlicesOut == 0)) {
    if (doFlush) {
      doFlush = 0; // flushing is complete if we are now idle
    }
    return Thread::CallbackResult::Idle;
  }

  return Thread::CallbackResult::Ok;
}

DataBlockSlicer::DataBlockSlicer() {
  partialSlices.resize(maxLinks + 1);
  for (unsigned int i = 0; i <= maxLinks; i++) {
    if (i < maxLinks) {
      partialSlices[i].linkId = i;
    } else {
      partialSlices[i].linkId = undefinedLinkId;
    }
    partialSlices[i].tfId = undefinedTimeframeId;
    partialSlices[i].currentDataSet = nullptr;
  }
}
DataBlockSlicer::~DataBlockSlicer() {}

int DataBlockSlicer::appendBlock(DataBlockContainerReference const &block,
                                 double timestamp) {
  uint64_t tfId = block->getData()->header.timeframeId;
  uint8_t linkId = block->getData()->header.linkId;

  if (linkId != undefinedLinkId) {  
    if (linkId >= maxLinks) {
      theLog.log("wrong link id %d > %d", linkId, maxLinks - 1); 
      return -1;
    }
  } else {
    linkId = maxLinks;
  }
  
  

  // theLog.log("slicer %p append block link %d for tf %d",
  //   this,(int)linkId,(int)tfId);

  if (partialSlices[linkId].currentDataSet != nullptr) {
    // theLog.log("slice size = %d
    // chunks",partialSlices[linkId].currentDataSet->size()); if
    // ((partialSlices[linkId].tfId!=tfId)||(partialSlices[linkId].currentDataSet->size()>2))
    // {
    if ((partialSlices[linkId].tfId != tfId) ||
        (tfId == undefinedTimeframeId)) {
      // the current slice is complete
      // theLog.log("slicer %d TF %d link %d is complete (%d blocks)",slicerId,
      // partialSlices[linkId].tfId,linkId,partialSlices[linkId].currentDataSet->size());
      slices.push(partialSlices[linkId].currentDataSet);
      partialSlices[linkId].currentDataSet = nullptr;
    }
  }
  if (partialSlices[linkId].currentDataSet == nullptr) {
    // printf("creating STF %d\n",(int)stfid);
    try {
      partialSlices[linkId].currentDataSet = std::make_shared<DataSet>();
    } catch (...) {
      return -1;
    }
  }
  partialSlices[linkId].currentDataSet->push_back(block);
  partialSlices[linkId].tfId = tfId;
  partialSlices[linkId].lastUpdateTime = timestamp;
  return partialSlices[linkId].currentDataSet->size();
}

DataSetReference DataBlockSlicer::getSlice(bool includeIncomplete) {
  // get a slice. get oldest from queue, or possibly currentDataSet when queue
  // empty and includeIncomplete is true
  DataSetReference bcv = nullptr;
  if (slices.empty()) {
    if (includeIncomplete) {
      for (auto &c : partialSlices) {
        bcv = c.currentDataSet;
        if (bcv != nullptr) {
          c.currentDataSet = nullptr;
          break;
        }
      }
    } else {
      return nullptr;
    }
  } else {
    bcv = slices.front();
    slices.pop();
  }
  return bcv;
}

int DataBlockSlicer::completeSliceOnTimeout(double timestamp) {
  int nFlushed = 0;
  for (unsigned int i = 0; i <= maxLinks; i++) {
    // check if current data set needs to be flushed
    if (partialSlices[i].currentDataSet != nullptr) {
      if (partialSlices[i].lastUpdateTime <= timestamp) {
        slices.push(partialSlices[i].currentDataSet);
        partialSlices[i].currentDataSet = nullptr;
        nFlushed++;
      }
    }
  }
  return nFlushed;
}
