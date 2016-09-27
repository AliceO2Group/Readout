///
/// @file    mainReadout.cxx
/// @author  Sylvain
///
#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>
#include <DataFormat/DataBlock.h>
#include <DataFormat/DataBlockContainer.h>
#include <DataFormat/MemPool.h>

#include <atomic>
#include <malloc.h>
#include <boost/format.hpp>
#include <chrono>

  
//#include "CThread.h"

#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;
  
 
  
  
  

  
  
  
  
  
Thread::CallbackResult  testloop(void *arg) {
  printf("testloop ( %p)...\n",arg);
  sleep(1);
  return Thread::CallbackResult::Ok;
}



class CReadout {
  public:
  CReadout(ConfigFile *cfg, std::string name="readout");
  virtual ~CReadout();
  
  DataBlockContainer *getBlock();

  void start();
  void stop();

//  protected: 
// todo: give direct access to output FIFO?
  AliceO2::Common::Fifo<DataBlockContainer> *dataOut;

  private:
  Thread *readoutThread;  
  static Thread::CallbackResult  threadCallback(void *arg);
  virtual Thread::CallbackResult  populateFifoOut()=0;  // function called iteratively in dedicated thread to populate FIFO
  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;
  
  unsigned long long nBlocksOut;
  double readoutRate;
};

CReadout::CReadout(ConfigFile *cfg, std::string name) {
  // todo: take thread name from config, or as argument
  
  readoutThread=new Thread(CReadout::threadCallback,this,name,1000);
  //readoutThread->start();
  int outFifoSize=1000;
  dataOut=new AliceO2::Common::Fifo<DataBlockContainer>(outFifoSize);
  
  readoutRate=10.0; //(target readout rate in Hz)
  readoutRate=cfg->getValue<double>("readout.rate");

  nBlocksOut=0;
}

void CReadout::start() {
  readoutThread->start();
  clk.reset(1000000.0/readoutRate);
  clk0.reset();
}

void CReadout::stop() {
  readoutThread->stop();
  //printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocksOut,clk0.getTimer(),nBlocksOut/clk0.getTime());
  readoutThread->join();
}

CReadout::~CReadout() {
  delete readoutThread;
  delete dataOut;
}

DataBlockContainer* CReadout::getBlock() {
  DataBlockContainer *b=NULL;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult  CReadout::threadCallback(void *arg) {
  CReadout *ptr=(CReadout *)arg;
  //printf("cb = %p\n",arg);
  //return TTHREAD_LOOP_CB_IDLE;
  
  // todo: check rate reached
    
  if ((!ptr->clk.isTimeout()) && (ptr->nBlocksOut!=0) && (ptr->nBlocksOut>=ptr->readoutRate*ptr->clk0.getTime())) {
    return Thread::CallbackResult::Idle;
  }
  
  Thread::CallbackResult  res=ptr->populateFifoOut();
  if (res==Thread::CallbackResult::Ok) {
    //printf("new data @ %lf - %lf\n",ptr->clk0.getTime(),ptr->clk.getTime());
    ptr->clk.increment();
    ptr->nBlocksOut++;
  }
  return res;
}

class CReadoutDummy : public CReadout {

  public:
    CReadoutDummy(ConfigFile *cfg, std::string name="dummyReadout");
    ~CReadoutDummy();
  
  private:
    MemPool *mp;
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;    
};


CReadoutDummy::CReadoutDummy(ConfigFile *cfg, std::string name) : CReadout(cfg, name) {
  mp=new MemPool(1000,1024*1024);
  currentId=0;
}

CReadoutDummy::~CReadoutDummy() {
  delete mp;
} 

Thread::CallbackResult  CReadoutDummy::populateFifoOut() {
  if (dataOut->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  DataBlockContainer *d=NULL;
  try {
    d=new DataBlockContainerFromMemPool(mp);
  }
  catch (std::string err) {
    return Thread::CallbackResult::Idle;
  }
  //printf("push %p\n",(void *)d);
  
  if (d==NULL) {
    return Thread::CallbackResult::Idle;
  }
  
  DataBlock *b=d->getData();
  
  int dSize=(int)(rand()*1000.0/RAND_MAX);
  //dSize=100;
  //printf("%d\n",dSize);
  
  currentId++;  // don't start from 0

  b->header.blockType=DataBlockType::H_BASE;
  b->header.headerSize=sizeof(DataBlockHeaderBase);
  b->header.dataSize=dSize;
  b->header.id=currentId;
  

  
  // push new page to mem
  dataOut->push(d); 

  //printf("populateFifoOut()\n");
  return Thread::CallbackResult::Ok;
}


// keeps track of mem alloc free
// use shared_ptr for data blocks tracking ???





class CAggregator {
  public:
  CAggregator(AliceO2::Common::Fifo<std::vector<DataBlockContainer *>> *output, std::string name="Aggregator");
  ~CAggregator();
  
  int addInput(AliceO2::Common::Fifo<DataBlockContainer> *input); // add a FIFO to be used as input
  
  void start(); // starts processing thread
  void stop(int waitStopped=1);  // stop processing thread (and possibly wait it terminates)


  static Thread::CallbackResult  threadCallback(void *arg);  
 
  private:
  std::vector<AliceO2::Common::Fifo<DataBlockContainer> *> inputs;
  AliceO2::Common::Fifo<std::vector<DataBlockContainer *>> *output;
  
  Thread *aggregateThread;
  AliceO2::Common::Timer incompletePendingTimer;
  int isIncompletePending;
};

CAggregator::CAggregator(AliceO2::Common::Fifo<std::vector<DataBlockContainer *>> *v_output, std::string name){
  output=v_output;
  aggregateThread=new Thread(CAggregator::threadCallback,this,name,100);
  isIncompletePending=0;
}

CAggregator::~CAggregator() {
  // todo: flush out FIFOs ?
  delete aggregateThread;
}
  
int CAggregator::addInput(AliceO2::Common::Fifo<DataBlockContainer> *input) {
  //inputs.push_back(input);
  inputs.push_back(input);
  return 0;
}

Thread::CallbackResult CAggregator::threadCallback(void *arg) {
  CAggregator *dPtr=(CAggregator*)arg;
  if (dPtr==NULL) {
    return Thread::CallbackResult::Error;
  }
  
  int someEmpty=0;
  int allEmpty=1;
  int allSame=1;
  DataBlockId minId=0;
  DataBlockId lastId=0;
  // todo: add invalidId instead of 0 for undefined value
  
  for (unsigned int i=0; i<dPtr->inputs.size(); i++) {
    if (!dPtr->inputs[i]->isEmpty()) {
      allEmpty=0;
      DataBlock *b=dPtr->inputs[i]->front()->getData();
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
  
  std::vector<DataBlockContainer *> *bcv=new std::vector<DataBlockContainer *>();
  if (bcv==NULL) {
    return Thread::CallbackResult::Error;
  }
  
  
  for (unsigned int i=0; i<dPtr->inputs.size(); i++) {
    if (!dPtr->inputs[i]->isEmpty()) {
      DataBlockContainer *b=dPtr->inputs[i]->front();    
      DataBlockId newId=b->getData()->header.id;
      if (newId==minId) {
        bcv->push_back(b);
        dPtr->inputs[i]->pop(b);
        //printf("1 block for event %llu from input %d @ %p\n",(unsigned long long)newId,i,(void *)b);
      }
    }
  }
  
  //if (!allSame) {printf("!incomplete block pushed\n");}
  dPtr->output->push(bcv);

  // todo: add timeout for standalone pieces - or wait if some FIFOs empty
  // add flag in output data to say it is incomplete
  
  return Thread::CallbackResult::Ok;
}
 
void CAggregator::start() {
  aggregateThread->start();
}

void CAggregator::stop(int waitStop) {
  aggregateThread->stop();
  if (waitStop) {
    aggregateThread->join();
  }
}


/*

Equipment:
configure, start, stop...


internal thread:
push new data to FIFO

external interface:
getBlock (from internal FIFO)




Readout:
instanciate equipments
readout FIFOs

aggregate subevents in FLP time frame? (need to order equipments or not?)
copy data
release data page



class for data aggregation:
push data from different links/equipments,
group by time, source


data processing : thread pool
output: new data pages, release input pages
... or overwrite input pages?



general params:
fifoSize : output fifo size
dropPolicy : (drop or block when FIFO full?)

dataPageSize -> size of each data page
dataPageNumber -> number of data pages


should be allocated by thread

to add: freePage callback



datablock: add "releaseMem() callback + arg" for each block


dummy readout:
rate
sizeMean
sizeSigma

inputFile (preload)

TODO:

support class for data format 
(with/without allocator)

populate with FLP data format

method to append something to datablock (copy or reserve space) and update headers up
use of std:shared_ptr to count refs and release ?

http://stackoverflow.com/questions/15137626/additional-arguments-for-custom-deleter-of-shared-ptr

lambda?


ns time unit


ID (timeframe?)

ticks (min ticks) time-base? ns? ref clock? ...

event-building class:

for each miniframe: expected number of contributors, timeout, etc


TODO:
config: get a list from file
iterate sections

go to section index and pass pointer to sub-part of tree

ptree getSubTree(const string& path)
{
     return pt.get_child(path);
}

how to get epoch from LHC ? (T0)


FLP hackaton meeting 3rd



*/









int main(int argc, char* argv[])
{
/*
  #define N_PAGES 10
  
  MemPool *mp;
  
  try {
    mp=new MemPool(N_PAGES,1024*1024);
  }
  catch (std::string err) {
    std::cout << "Failed to create memory pool: " << err << std::endl;
    return 1;
  }
  
 
  DataBlockContainerFromMemPool *dc;
  dc=new DataBlockContainerFromMemPool(mp);
  delete dc; 
  
  void *ptr[N_PAGES];
  for (int i=0;i<N_PAGES+1;i++) {
    ptr[i]=mp->getPage();
    if (ptr[i]!=NULL) {
      printf("ptr[%d]=%p\n",i,ptr[i]);
    } else {
      printf("failed @ %d\n",i);
    }
  }
  for (int i=0;i<N_PAGES+1;i++) {
    mp->releasePage(ptr[i]);
  }
  
  delete mp;
  

  return 0;
*/


  InfoLogger theLog;
  ConfigFile cfg;


  const char* cfgFile="";
  if (argc<2) {
    printf("Please provide path to configuration file\n");
    return -1;
  }
  cfgFile=argv[1];
  theLog.log("Readout starting");
  theLog.log("Reading configuration from %s",cfgFile);  

  std::string cfgEquipmentType="";
  try {
    cfg.load(cfgFile);
    cfgEquipmentType=cfg.getValue<std::string>("readout.equipmentType");
  }
  catch (std::string err) {
    theLog.log("Error : %s",err.c_str());
    return -1;
  }

  /*
  CReadout *r=NULL; 
  if (cfgEquipmentType=="dummy") {
    r=new CReadoutDummy(&cfg);
  } else {
    theLog.log("Unknown equipment type %s",cfgEquipmentType.c_str());
  }
  */
  
  
  AliceO2::Common::Fifo<std::vector<DataBlockContainer *>> agg_output(1000);  
  CAggregator agg(&agg_output,"Aggregator");

  int nReadoutDevice=10;
  CReadout **readoutDevices=new CReadout* [nReadoutDevice];
  
  for (int i=0;i<nReadoutDevice;i++) {
    std::string name = str( boost::format("Readout %d") % i );
    readoutDevices[i]=new CReadoutDummy(&cfg,name);
    agg.addInput(readoutDevices[i]->dataOut);
  }
  agg.start();
  
  /*CReadout *r2=new CReadoutDummy(&cfg);   
  agg.addInput(r->dataOut);
  agg.addInput(r2->dataOut);

  agg.start();
  r2->start();
  */

  for (int i=0;i<nReadoutDevice;i++) {
    readoutDevices[i]->start();
  }

  
  
  AliceO2::Common::Timer t;
  t.reset(5000000);
  int isRunning=1;
  //r->start();
  AliceO2::Common::Timer t0;
  t0.reset();
  unsigned long long nBlocks=0;
  unsigned long long nBytes=0;
  double t1;

  while (1) {
    if (t.isTimeout()) {
      if (isRunning) {
        isRunning=0;
        theLog.log("stopping readout");
        for (int i=0;i<nReadoutDevice;i++) {
          readoutDevices[i]->stop();
        }
        /*
        r->stop();
        r2->stop();
        */
        t.reset(1000000);  // add a delay before stopping aggregator
        t1=t0.getTime();
        theLog.log("readout stopped");
      } else {
        break;
      }
    }
    //DataBlockContainer *newBlock=NULL;
    //newBlock=r->getBlock();

    std::vector<DataBlockContainer *> *bc=NULL;
    agg_output.pop(bc);

    if (bc!=NULL) {
      unsigned int nb=(int)bc->size();
      //printf("received 1 vector made of %u blocks\n",nb);

      for (unsigned int i=0;i<nb;i++) {
        //printf("pop %d\n",i);
        DataBlockContainer *b=bc->at(i);

        nBlocks++;
        nBytes+=b->getData()->header.dataSize;

        delete b;
        //printf("pop %p\n",(void *)b);

      }
      delete bc;      
    } else {
      usleep(100);
    }

    /*
    if (newBlock==NULL)  {
      usleep(100);
    } else {
      nBlocks++;
      nBytes+=newBlock->getData()->header.dataSize;
      delete newBlock;
    }
    */

  }

  printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocks,t1,nBlocks/t1);
  printf("%.1lf MB received\n",nBytes/(1024.0*1024.0));
    
/*  if (r!=NULL) {
  }
*/
  theLog.log("stopping aggregator");
  agg.stop();
  theLog.log("aggregator stopped");
  
  for (int i=0;i<nReadoutDevice;i++) {
    delete readoutDevices[i];
    readoutDevices[i]=NULL;
  }
  delete [] readoutDevices;
    
  /*  
  delete r2;  
  if (r!=NULL) {
    delete r;
  }
  */
  return 0;
  
  

  void *p=NULL;
  Thread tt(testloop,p,"Thread");
  tt.start();
  sleep(2);





  return 0;  
 
}
