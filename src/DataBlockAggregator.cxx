// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "DataBlockAggregator.h"
#include "readoutInfoLogger.h"
#include "MemoryPagesPool.h"
#include <inttypes.h>

DataBlockAggregator::DataBlockAggregator(AliceO2::Common::Fifo<DataSetReference>* v_output, std::string name)
{
  output = v_output;
  aggregateThread = std::make_unique<Thread>(DataBlockAggregator::threadCallback, this, name, 1000);
  isIncompletePending = 0;
}

DataBlockAggregator::~DataBlockAggregator()
{
  // todo: flush out FIFOs ?
  aggregateThread->join();
}

int DataBlockAggregator::addInput(std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> input)
{
  // inputs.push_back(input);
  inputs.push_back(input);
  slicers.push_back(DataBlockSlicer());
  return 0;
}

Thread::CallbackResult DataBlockAggregator::threadCallback(void* arg)
{
  DataBlockAggregator* dPtr = (DataBlockAggregator*)arg;
  if (dPtr == NULL) {
    return Thread::CallbackResult::Error;
  }

  if (!dPtr->isThreadNamed) {
    #ifdef _GNU_SOURCE
      pthread_setname_np(pthread_self(), "aggregator");
    #endif
    dPtr->isThreadNamed = 1;
  }

  if (dPtr->output->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  return dPtr->executeCallback();
}

void DataBlockAggregator::start()
{
  reset();
  aggregateThread->start();
}

void DataBlockAggregator::stop(int waitStop)
{
  doFlush = 0;
  aggregateThread->stop();
  if (waitStop) {
    aggregateThread->join();
  }
  theLog.log(LogInfoDevel_(3003), "Aggregator processed %llu blocks", totalBlocksIn);
  for (unsigned int i = 0; i < inputs.size(); i++) {

    // printf("aggregator input %d: in=%llu out=%llu\n",i,inputs[i]->getNumberIn(),inputs[i]->getNumberOut());
    // printf("Aggregator FIFO in %d clear: %d items\n",i,inputs[i]->getNumberOfUsedSlots());

    inputs[i]->clear();
  }
  // printf("Aggregator FIFO out after clear: %d items\n",output->getNumberOfUsedSlots());
  // TODO: do we really need to clear? should be automatic

  DataSetReference bc = nullptr;
  while (!output->pop(bc)) {
    bc->clear();
  }
  output->clear();
  
  reset();
}

Thread::CallbackResult DataBlockAggregator::executeCallback()
{

  if (output->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  unsigned int nInputs = inputs.size();
  unsigned int nBlocksIn = 0;
  unsigned int nSlicesOut = 0;

  // get time once per iteration
  double now = timeNow.getTime();

  // flush pending data
  // check here to ensure it does not start in middle of a loop
  // because doFlush is set asynchronously
  bool executeFlush = 0;
  if (doFlush) {
    executeFlush = 1;
  }

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
      updatePageStateFromDataBlockContainerReference(b, MemoryPage::PageState::InAggregatorFifoOut);
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
      updatePageStateFromDataBlockContainerReference(b, MemoryPage::PageState::InAggregator);
      nBlocksIn++;
      totalBlocksIn++;
      // printf("Got block %d from dev %d eq %d link %d tf %d\n", (int)(b->getData()->header.blockId), i, (int)(b->getData()->header.equipmentId), (int)(b->getData()->header.linkId), (int)(b->getData()->header.timeframeId));
      if (slicers[i].appendBlock(b, now) <= 0) {
        return Thread::CallbackResult::Error;
      }
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
      if ((executeFlush) && (inputs[i]->isEmpty())) {
        includeIncomplete = 1;
      }
      DataSetReference bcv = slicers[i].getSlice(includeIncomplete);
      if (bcv == nullptr) {
        break;
      }

      if (enableStfBuilding) {
        // buffer timeframes
        DataBlockContainerReference b = bcv->at(0);
        DataBlock* db = b->getData();
        uint64_t tfId = db->header.timeframeId;
        uint64_t sourceId = (((uint64_t)db->header.equipmentId) << 32) | ((uint64_t)db->header.linkId);
        if (tfId <= lastTimeframeId) {
	  static InfoLogger::AutoMuteToken token(LogWarningSupport_(3004));
          theLog.log(token, "Discarding late data for TF %" PRIu64 " (source = 0x%" PRIx64 ")", tfId, sourceId);
        } else {
          tStf& stf = stfBuffer[tfId];
          stf.tfId = tfId;
          stf.sstf.push_back({ sourceId, bcv, now });
          stf.updateTime = now;
          // theLog.log(LogDebugTrace, "aggregate - added tf %lu : source %lX",tfId,sourceId);
        }
      } else {
        for(auto const& b : *bcv) {
            updatePageStateFromDataBlockContainerReference(b, MemoryPage::PageState::InAggregatorFifoOut);
        }
        // push directly out completed slices
        output->push(bcv);
      }

      nSlicesOut++;
      // printf("Pushed STF : %d chunks\n",(int)bcv->size());
    }
  }
  // on next iteration, start from a different input to balance equipments emptying order
  if (nInputs>1) {
    nextIndex = (nextIndex + 1) % nInputs;
  }

  // in TF buffering mode, are there some complete timeframes?
  if (enableStfBuilding) {
    int nDataSetPushed = 0;
    int nStfPushed = 0;
    auto it = stfBuffer.begin();
    while (it != stfBuffer.end()) {
      double age = now - it->second.updateTime;
      if ((age >= cfgStfTimeout) || (executeFlush)) {
        // printf("pushing age %.3f tf %d -> %d sources\n",age,(int)it->second.tfId,(int)it->second.sstf.size());
        double tmin = it->second.updateTime;
        double tmax = it->second.updateTime;
        int ix = 0;
        for (auto const& ss : it->second.sstf) {
          for(auto const& b : *ss.data) {
            updatePageStateFromDataBlockContainerReference(b, MemoryPage::PageState::InAggregatorFifoOut);
          }
          ix++;
          if (ix == (int)it->second.sstf.size()) {
            // this is the last piece of this TF, mark last block as such
            ss.data->back()->getData()->header.flagEndOfTimeframe = 1;
          }
          output->push(ss.data);
          nDataSetPushed++;
          if (ss.updateTime < tmin) {
            tmin = ss.updateTime;
          }
          if (ss.updateTime < tmax) {
            tmax = ss.updateTime;
          }
        }
        if (it->second.tfId == 1) {
          nSources = it->second.sstf.size(); // keep track of number of sources in first TF
        }
        nStfPushed++;
	uint64_t newTimeframeId = it->second.tfId;
	if (newTimeframeId > lastTimeframeId + 1) {
	  static InfoLogger::AutoMuteToken token(LogWarningSupport_(3004));
          theLog.log(token, "Gap in timeframe ids detected: previous = %" PRIu64 " new = %" PRIu64, lastTimeframeId, newTimeframeId);
	}
        lastTimeframeId = newTimeframeId;
        /*
        if (lastTimeframeId % 10 == 1) {
          theLog.log(LogDebugTrace, "LastTimeframeId=%lu deltaT=%f",lastTimeframeId,tmax-tmin);
        }
        */
        it = stfBuffer.erase(it);
      } else {
        break;
      }
    }
  }

  if ((nBlocksIn == 0) && (nSlicesOut == 0)) {
    if ((executeFlush) && (stfBuffer.size() == 0)) {
      doFlush = 0; // flushing is complete if we are now idle
    }
    return Thread::CallbackResult::Idle;
  }

  return Thread::CallbackResult::Ok;
}

DataBlockSlicer::DataBlockSlicer() {
  reset();
}

DataBlockSlicer::~DataBlockSlicer() {}

int DataBlockSlicer::appendBlock(DataBlockContainerReference const& block, double timestamp)
{
  uint64_t tfId = block->getData()->header.timeframeId;
  DataSourceId sourceId;
  sourceId.linkId = block->getData()->header.linkId;
  sourceId.equipmentId = block->getData()->header.equipmentId;

  if (sourceId.linkId != undefinedLinkId) {
    if (sourceId.linkId >= maxLinks) {
      static InfoLogger::AutoMuteToken token(LogWarningSupport_(3004));
      theLog.log(token, "wrong link id %d > %d", sourceId.linkId, maxLinks - 1);
      return -1;
    }
  }

  // theLog.log(LogDebugTrace, "slicer %p append block eq %d link %d for tf %d", this,(int)sourceId.equipmentId,(int)sourceId.linkId,(int)tfId);
  PartialSlice& s = partialSlices[sourceId];

  if (s.currentDataSet != nullptr) {
    // theLog.log(LogDebugTrace, "slice size = %d chunks",(int)s.currentDataSet->size());
    if ((s.tfId != tfId) || (tfId == undefinedTimeframeId)) {
      // the current slice is complete
      // theLog.log(LogDebugTrace, "slicer %p TF %d eq %d link %d is complete (%d blocks)",this, (int)s.tfId,(int)sourceId.equipmentId,(int)sourceId.linkId,(int)s.currentDataSet->size());
      slices.push(s.currentDataSet);
      s.currentDataSet = nullptr;
    }
  }
  if (s.currentDataSet == nullptr) {
    try {
      s.currentDataSet = std::make_shared<DataSet>();
    } catch (...) {
      return -1;
    }
  }
  s.currentDataSet->push_back(block);
  s.tfId = tfId;
  s.lastUpdateTime = timestamp;
  // printf(" %d,%d -> %d blocks\n",(int)sourceId.equipmentId,(int)sourceId.linkId,(int)s.currentDataSet->size());
  return s.currentDataSet->size();
}

DataSetReference DataBlockSlicer::getSlice(bool includeIncomplete)
{
  // get a slice. get oldest from queue, or possibly currentDataSet when queue empty and includeIncomplete is true
  DataSetReference bcv = nullptr;
  if (slices.empty()) {
    if (includeIncomplete) {
      for (auto& s : partialSlices) {
        bcv = s.second.currentDataSet;
        if (bcv != nullptr) {
          s.second.currentDataSet = nullptr;
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
  // printf("getSlice -> %p\n", bcv.get());
  return bcv;
}

int DataBlockSlicer::completeSliceOnTimeout(double timestamp)
{
  int nFlushed = 0;
  for (auto& s : partialSlices) {
    // check if current data set needs to be flushed
    if (s.second.currentDataSet != nullptr) {
      if (s.second.lastUpdateTime <= timestamp) {
        slices.push(s.second.currentDataSet);
        s.second.currentDataSet = nullptr;
        nFlushed++;
      }
    }
  }
  return nFlushed;
}

void DataBlockSlicer::reset()
{
  slicerId = -1;
  
  // empty buffers
  while(!slices.empty()) {
    auto bc = slices.front();
    bc->clear();
    slices.pop();
  }
  for (auto& s : partialSlices) {
    s.second.currentDataSet = nullptr;
  }
  partialSlices.clear();
}

void DataBlockAggregator::reset()
{
  // reset slicers
  for (unsigned int ix = 0; ix < inputs.size(); ix++) {
    slicers[ix].reset();
    slicers[ix].slicerId = ix;
  }
  
  // reset buffers
  for(auto const & m : stfBuffer) {
    auto vsstf = m.second.sstf;
    for (auto& sstf : vsstf) {
      sstf.data = nullptr;
    }
    vsstf.clear();
  }
  stfBuffer.clear();
  
  // reset counters
  doFlush = 0;
  timeNow.reset();
  nSources = 0;
  nextIndex = 0;
  totalBlocksIn = 0;
  lastTimeframeId = 0;
}

