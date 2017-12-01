#include "DataBlockAggregator.h"


DataBlockAggregator::DataBlockAggregator(AliceO2::Common::Fifo<DataSetReference> *v_output, std::string name){
  output=v_output;
  aggregateThread=std::make_unique<Thread>(DataBlockAggregator::threadCallback,this,name,100);
  isIncompletePending=0;
}

DataBlockAggregator::~DataBlockAggregator() {
  // todo: flush out FIFOs ?
}
  
int DataBlockAggregator::addInput(std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>input) {
  //inputs.push_back(input);
  inputs.push_back(input);
  slicers.push_back(DataBlockSlicer());
  return 0;
}

Thread::CallbackResult DataBlockAggregator::threadCallback(void *arg) {
  DataBlockAggregator *dPtr=(DataBlockAggregator*)arg;
  if (dPtr==NULL) {
    return Thread::CallbackResult::Error;
  }
   
  if (dPtr->output->isFull()) {
    return Thread::CallbackResult::Idle;
  }
   
  return dPtr->executeCallback();
  
  /*
  // old implementation
   
  int someEmpty=0;
  int allEmpty=1;
  int allSame=1;
  DataBlockId minId=0;
  DataBlockId lastId=0;
  // todo: add invalidId instead of 0 for undefined value
  
  for (unsigned int i=0; i<dPtr->inputs.size(); i++) {
    if (!dPtr->inputs[i]->isEmpty()) {
      allEmpty=0;
      DataBlockContainerReference bc=nullptr;
      dPtr->inputs[i]->front(bc);
      DataBlock *b=bc->getData();
      DataBlockId newId=b->header.id;
      if ((minId==0)||(newId<minId)) {
        minId=newId;
      }
      if ((lastId!=0)&&(newId!=lastId)) {
        allSame=0;
      }
      lastId=newId;
    } else {
      someEmpty=1;
      allSame=0;
    }
  }

  if (allEmpty) {
    return Thread::CallbackResult::Idle;
  }
  
  if (someEmpty) {
    if (!dPtr->isIncompletePending) {
      dPtr->incompletePendingTimer.reset(500000);
      dPtr->isIncompletePending=1;
    }

    if (dPtr->isIncompletePending && (!dPtr->incompletePendingTimer.isTimeout())) {
      return Thread::CallbackResult::Idle;
    }
  }

  if (allSame) {
    dPtr->isIncompletePending=0;
  } 
  
  DataSetReference bcv=nullptr;
  try {
    bcv=std::make_shared<DataSet>();
  }
  catch(...) {
    return Thread::CallbackResult::Error;
  }
  
  
  for (unsigned int i=0; i<dPtr->inputs.size(); i++) {
    if (!dPtr->inputs[i]->isEmpty()) {    
      DataBlockContainerReference b=nullptr;
      dPtr->inputs[i]->front(b);
      DataBlockId newId=b->getData()->header.id;
      if (newId==minId) {
        bcv->push_back(b);
        dPtr->inputs[i]->pop(b);
        //printf("1 block for event %llu from input %d @ %p\n",(unsigned long long)newId,b);
	//printf("aggregating %p into dataSet %p\n",b->getData()->data,bcv.get());
      }
    }
  }
  
  //if (!allSame) {printf("!incomplete block pushed\n");}
  // todo: add error check
  dPtr->output->push(bcv);
  
//  printf("readout output: pushed %llu\n",dPtr->output->getNumberIn());
  // todo: add timeout for standalone pieces - or wait if some FIFOs empty
  // add flag in output data to say it is incomplete
  //printf("agg: new block\n");
  return Thread::CallbackResult::Ok;
  
  // end of old implementation
  */
}
 
void DataBlockAggregator::start() {
  aggregateThread->start();
}

void DataBlockAggregator::stop(int waitStop) {
  aggregateThread->stop();
  if (waitStop) {
    aggregateThread->join();
  }
  for (unsigned int i=0; i<inputs.size(); i++) {

//    printf("aggregator input %d: in=%llu  out=%llu\n",i,inputs[i]->getNumberIn(),inputs[i]->getNumberOut());      
//    printf("Aggregator FIFO in %d clear: %d items\n",i,inputs[i]->getNumberOfUsedSlots());
    
    inputs[i]->clear();
  }
//  printf("Aggregator FIFO out after clear: %d items\n",output->getNumberOfUsedSlots());
  /* todo: do we really need to clear? should be automatic */
  
  DataSetReference bc=nullptr;
  while (!output->pop(bc)) {
    bc->clear();
  }
  output->clear();
}




Thread::CallbackResult DataBlockAggregator::executeCallback() {

  if (output->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  unsigned int nInputs=inputs.size();
  unsigned int nBlocksIn=0;
  unsigned int nSlicesOut=0;
  
  for (unsigned int ix=0; ix<nInputs; ix++) {
    int i=(ix + nextIndex) % nInputs;

    for (int j=0;j<1024;j++) {
      if (inputs[i]->isEmpty()) {
        break;
      }
      DataBlockContainerReference b=nullptr;
      inputs[i]->pop(b);
      nBlocksIn++;
      //printf("Got block %d from dev %d\n",(int)(b->getData()->header.id),i);
      slicers[i].appendBlock(b);
    }

    for (int j=0;j<1024;j++) {
      if (output->isFull()) {
        return Thread::CallbackResult::Idle;
      }

      DataSetReference bcv=slicers[i].getSlice();
      if (bcv==nullptr) {
        break;
      }
      output->push(bcv);
      nSlicesOut++;
      nextIndex=i+1;
      //printf("Pushed STF : %d chunks\n",(int)bcv->size());
    }
    
  }

  if ((nBlocksIn==0)&&(nSlicesOut==0)) {
    return Thread::CallbackResult::Idle;
  }
   
  return Thread::CallbackResult::Ok;
}




DataBlockSlicer::DataBlockSlicer() {
  currentId=0;
  currentDataSet=nullptr;
}
DataBlockSlicer::~DataBlockSlicer() {
}
  
int DataBlockSlicer::appendBlock(DataBlockContainerReference const &block) {
  uint64_t stfid=1+(block->getData()->header.id-1) / 8;  
  //printf("slicing block into stf %d\n",(int)stfid);
  if (currentDataSet!=nullptr) {   
    if (stfid!=currentId) {
      // the current slice is complete
      slices.push(currentDataSet);
      currentDataSet=nullptr;
    }
  }
  if (currentDataSet==nullptr) {
    //printf("creating STF %d\n",(int)stfid);
    try {
        currentDataSet=std::make_shared<DataSet>();
    }
    catch (...) {
      return -1;
    }
  }
  currentDataSet->push_back(block);
  currentId=stfid;
  return currentDataSet->size();
}

DataSetReference DataBlockSlicer::getSlice(bool includeIncomplete) {
  // get a slice. get oldest from queue, or possibly currentDataSet when queue empty and includeIncomplete is true
  DataSetReference bcv=nullptr;
  if (slices.empty()) {
    if (includeIncomplete) {
      bcv=currentDataSet;
      currentDataSet=nullptr;
    } else {
      return nullptr;
    }
  } else {
    bcv=slices.front();
    slices.pop();
  }
  return bcv;
}

