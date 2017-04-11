///
/// @file    mainReadout.cxx
/// @author  Sylvain
///

#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>
#include <DataFormat/DataBlock.h>
#include <DataFormat/DataBlockContainer.h>
#include <DataFormat/MemPool.h>
#include <DataFormat/DataSet.h>

#include <atomic>
#include <malloc.h>
#include <boost/format.hpp>
#include <chrono>
#include <signal.h>
#include <math.h>

#include <memory>

#include <DataSampling/InjectorFactory.h>
  
#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

#include "RORC/Parameters.h"
#include "RORC/ChannelFactory.h"
#include "RORC/MemoryMappedFile.h"
#include "RORC/ChannelMasterInterface.h"
#include "RORC/Exception.h"

#include <Monitoring/MonitoringFactory.h>

#ifdef WITH_FAIRMQ
#include <FairMQDevice.h>
#include <FairMQMessage.h>
#include <FairMQTransportFactory.h>
#include <FairMQTransportFactoryZMQ.h>
#include <FairMQProgOptions.h>
#endif

using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;
using namespace AliceO2::Monitoring;
  
#define LOG_TRACE printf("%d\n",__LINE__);fflush(stdout);


// global entry point to log system
InfoLogger theLog;
  

static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int){
  printf(" *** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest=1;
}



class ReadoutEquipment {
  public:
  ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint);
  virtual ~ReadoutEquipment();
  
  DataBlockContainerReference getBlock();

  void start();
  void stop();
  const std::string & getName();

//  protected: 
// todo: give direct access to output FIFO?
  std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> dataOut;

  private:
  std::unique_ptr<Thread> readoutThread;  
  static Thread::CallbackResult  threadCallback(void *arg);
  virtual Thread::CallbackResult  populateFifoOut()=0;  // function called iteratively in dedicated thread to populate FIFO
  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;
  
  unsigned long long nBlocksOut;
  double readoutRate;
  protected:
  std::string name;
};


ReadoutEquipment::ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint) {
  
  // example: browse config keys
  //for (auto cfgKey : ConfigFileBrowser (&cfg,"",cfgEntryPoint)) {
  //  std::string cfgValue=cfg.getValue<std::string>(cfgEntryPoint + "." + cfgKey);
  //  printf("%s.%s = %s\n",cfgEntryPoint.c_str(),cfgKey.c_str(),cfgValue.c_str());
  //}

  
  // by default, name the equipment as the config node entry point
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".name", name, cfgEntryPoint);

  // target readout rate in Hz, -1 for unlimited (default)
  cfg.getOptionalValue<double>("readout.rate",readoutRate,-1.0);


  readoutThread=std::make_unique<Thread>(ReadoutEquipment::threadCallback,this,name,1000);

  int outFifoSize=1000;
  
  dataOut=std::make_shared<AliceO2::Common::Fifo<DataBlockContainerReference>>(outFifoSize);
  nBlocksOut=0;
}

const std::string & ReadoutEquipment::getName() {
  return name;
}

void ReadoutEquipment::start() {
  readoutThread->start();
  if (readoutRate>0) {
    clk.reset(1000000.0/readoutRate);
  }
  clk0.reset();
}

void ReadoutEquipment::stop() {
  readoutThread->stop();
  //printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocksOut,clk0.getTimer(),nBlocksOut/clk0.getTime());
  readoutThread->join();
}

ReadoutEquipment::~ReadoutEquipment() {
//  printf("deleted %s\n",name.c_str());
}

DataBlockContainerReference ReadoutEquipment::getBlock() {
  DataBlockContainerReference b=nullptr;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult  ReadoutEquipment::threadCallback(void *arg) {
  ReadoutEquipment *ptr=static_cast<ReadoutEquipment *>(arg);
  //printf("cb = %p\n",arg);
  //return TTHREAD_LOOP_CB_IDLE;
  
  // todo: check rate reached
    
  if (ptr->readoutRate>0) {
    if ((!ptr->clk.isTimeout()) && (ptr->nBlocksOut!=0) && (ptr->nBlocksOut+1>ptr->readoutRate*ptr->clk0.getTime())) {
      return Thread::CallbackResult::Idle;
    }
  }
    
  Thread::CallbackResult  res=ptr->populateFifoOut();
  if (res==Thread::CallbackResult::Ok) {
    //printf("new data @ %lf - %lf\n",ptr->clk0.getTime(),ptr->clk.getTime());
    ptr->clk.increment();
    ptr->nBlocksOut++;
//    printf("%s : pushed block %d\n",ptr->name.c_str(), ptr->nBlocksOut);
  }
  return res;
}






class ReadoutEquipmentDummy : public ReadoutEquipment {

  public:
    ReadoutEquipmentDummy(ConfigFile &cfg, std::string name="dummyReadout");
    ~ReadoutEquipmentDummy();
  
  private:
    std::shared_ptr<MemPool> mp;
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    int eventMaxSize;
    int eventMinSize;    
};


ReadoutEquipmentDummy::ReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint) {

  int memPoolNumberOfElements=10000;
  int memPoolElementSize=0.01*1024*1024;

  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolNumberOfElements", memPoolNumberOfElements);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolElementSize", memPoolElementSize);

  mp=std::make_shared<MemPool>(memPoolNumberOfElements,memPoolElementSize);
  currentId=0;
  
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMaxSize", eventMaxSize, (int)1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMinSize", eventMinSize, (int)1024);
}

ReadoutEquipmentDummy::~ReadoutEquipmentDummy() {
  // check if mempool still referenced
  if (!mp.unique()) {
    printf("Warning: mempool still %d references\n",(int)mp.use_count());
  }
} 

Thread::CallbackResult  ReadoutEquipmentDummy::populateFifoOut() {
  if (dataOut->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  DataBlockContainerReference d=NULL;
  try {
    d=std::make_shared<DataBlockContainerFromMemPool>(mp);
  }
  catch (...) {
  //printf("full\n");
    return Thread::CallbackResult::Idle;
  }
  //printf("push %p\n",(void *)d);
  
  if (d==NULL) {
    return Thread::CallbackResult::Idle;
  }
  
  DataBlock *b=d->getData();
  
  int dSize=(int)(eventMinSize+(int)((eventMaxSize-eventMinSize)*(rand()*1.0/RAND_MAX)));
  
  
  //dSize=100;
  //printf("%d\n",dSize);

  // todo: check size fits in page!
  // todo: align begin of data
  
  
  currentId++;  // don't start from 0
  b->header.blockType=DataBlockType::H_BASE;
  b->header.headerSize=sizeof(DataBlockHeaderBase);
  b->header.dataSize=dSize;
  b->header.id=currentId;
  // say it's contiguous header+data 
  b->data=&(((char *)b)[sizeof(DataBlock)]);

  //printf("(1) header=%p\nbase=%p\nsize=%d,%d\n",(void *)&(b->header),b->data,(int)b->header.headerSize,(int)b->header.dataSize);
//  b->data=NULL;
  
  
 
  for (int k=0;k<100;k++) {
    //printf("[%d]=%p\n",k,&(b->data[k]));
    b->data[k]=(char)k;
  }
  
//  printf("(2)header=%p\nbase=%p\nsize=%d,%d\n",(void *)&(b->header),b->data,(int)b->header.headerSize,(int)b->header.dataSize);
    
  //usleep(10000);
  
  // push new page to mem
  dataOut->push(d); 

//  printf("readout dummy loop FIFO out= %d items\n",dataOut->getNumberOfUsedSlots());

  //printf("populateFifoOut()\n");
  return Thread::CallbackResult::Ok;
}











// a big block of memory for I/O
class ReadoutMemoryHandler {
  private:
    std::unique_ptr<AliceO2::Rorc::MemoryMappedFile> mMemoryMappedFile;
    
  public:
  size_t size;
  void * address;
  const int pageSize=1024*1024;

  std::unique_ptr<AliceO2::Common::Fifo<long>> pagesAvailable;  // we store offset (with respect to base address) of pages available
  
  ReadoutMemoryHandler(){
    size_t memorySize=200*1024*1024;
    try {
      mMemoryMappedFile=std::make_unique<AliceO2::Rorc::MemoryMappedFile>("/var/lib/hugetlbfs/global/pagesize-2MB/test",memorySize,false);
    }
    catch (const AliceO2::Rorc::MemoryMapException& e) {
      printf("Failed to allocate memory buffer : %s\n",e.what());
      exit(1);
    }
    size=mMemoryMappedFile->getSize();
    address=mMemoryMappedFile->getAddress();
   
    
    int nPages=size/pageSize;

    pagesAvailable=std::make_unique<AliceO2::Common::Fifo<long>>(nPages);
    
    if ((long)address % pageSize!=0) {
      printf("Warning: unaligned pages!\n");
    }
    for (int i=0;i<nPages;i++) {
      long offset=i*pageSize;
      void *page=&((uint8_t*)address)[offset];
      printf("%d : 0x%p\n",i,page);
      pagesAvailable->push(offset);
    }
    printf("%d pages (%.2f MB) available\n",nPages,pageSize*1.0/(1024*1024));
    
  }
  ~ReadoutMemoryHandler() {
  }
 
};
// todo: locks for thread-safe access at init time
ReadoutMemoryHandler mReadoutMemoryHandler;










class DataBlockContainerFromRORC : public DataBlockContainer {
  private:
  AliceO2::Rorc::ChannelFactory::MasterSharedPtr mChannel;
  AliceO2::Rorc::Superpage mSuperpage;
  
  public:
  DataBlockContainerFromRORC(AliceO2::Rorc::ChannelFactory::MasterSharedPtr channel, AliceO2::Rorc::Superpage  const & superpage) {   
    data=nullptr;
    try {
       data=new DataBlock;
    } catch (...) {
      throw __LINE__;
    }

    printf("container created for superpage %ld @ %p\n",(long)superpage.getOffset(),this);

    data->header.blockType=DataBlockType::H_BASE;
    data->header.headerSize=sizeof(DataBlockHeaderBase);
    data->header.dataSize=8*1024;
    
    uint32_t *ptr=(uint32_t *) & (((uint8_t *)mReadoutMemoryHandler.address)[mSuperpage.getOffset()]);
    
    data->header.id=*ptr;
    data->data=(char *)ptr;

    mSuperpage=superpage;
    mChannel=channel;
  }
  
  ~DataBlockContainerFromRORC() {
    mReadoutMemoryHandler.pagesAvailable->push(mSuperpage.getOffset());
    printf("released superpage %ld\n",mSuperpage.getOffset());
    if (data!=nullptr) {
      delete data;
    }
  }
};














class ReadoutEquipmentRORC : public ReadoutEquipment {

  public:
    ReadoutEquipmentRORC(ConfigFile &cfg, std::string name="rorcReadout");
    ~ReadoutEquipmentRORC();
  
  private:
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    AliceO2::Rorc::ChannelFactory::MasterSharedPtr channel;
    
    int pageCount=0;
    int isInitialized=0;
};



ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name) : ReadoutEquipment(cfg, name) {
  
  try {

    int serialNumber=cfg.getValue<int>(name + ".serial");
    int channelNumber=cfg.getValue<int>(name + ".channel");
  
    theLog.log("Opening RORC %d:%d",serialNumber,channelNumber);

    int mPageSize=1024*1024;

    AliceO2::Rorc::Parameters params;
    params.setCardId(serialNumber);
    params.setChannelNumber(channelNumber);
    params.setGeneratorPattern(AliceO2::Rorc::GeneratorPattern::Incremental);
    params.setBufferParameters(AliceO2::Rorc::BufferParameters::Memory {
      mReadoutMemoryHandler.address, mReadoutMemoryHandler.size
    }); // this registers the memory block for DMA

    channel = AliceO2::Rorc::ChannelFactory().getMaster(params);  
    channel->resetChannel(AliceO2::Rorc::ResetLevel::Rorc);
    channel->startDma();
  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e, 1) << "\n";
    return;
  }
  isInitialized=1;
}



ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {
  if (isInitialized) {
    channel->stopDma();
  }

  theLog.log("Equipment %s : %d pages read",name.c_str(),(int)pageCount);
}


Thread::CallbackResult  ReadoutEquipmentRORC::populateFifoOut() {
  if (!isInitialized) return  Thread::CallbackResult::Error;
  int isActive=0;
    
  // this is to be called periodically for driver internal business
  channel->fillSuperpages();
  
  // give free pages to the driver
  while (channel->getSuperpageQueueAvailable() != 0) {   
    long offset=0;
    if (mReadoutMemoryHandler.pagesAvailable->pop(offset)==0) {
      AliceO2::Rorc::Superpage superpage;
      superpage.offset=offset;
      superpage.size=mReadoutMemoryHandler.pageSize;
      superpage.userData=&mReadoutMemoryHandler;
      channel->pushSuperpage(superpage);
      isActive=1;
    } else {
      break;
    }
  }
    
  // check for completed pages
  while (!dataOut->isFull() && (channel->getSuperpageQueueCount()>0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in FIFO ... let's check its state
    if (superpage.isFilled()) {
      std::shared_ptr<DataBlockContainerFromRORC>d=nullptr;
      try {
        d=std::make_shared<DataBlockContainerFromRORC>(channel, superpage);
      }
      catch (...) {
        break;
      }
      channel->popSuperpage();
      dataOut->push(d); 
      //d=nullptr;
      
      pageCount++;
      printf("read page %ld - %d\n",superpage.getOffset(),d.use_count());
      isActive=1;
    } else {
      break;
    }
  }
  //return Thread::CallbackResult::Ok;
  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}







class DataBlockAggregator {
  public:
  DataBlockAggregator(AliceO2::Common::Fifo<DataSetReference> *output, std::string name="Aggregator");
  ~DataBlockAggregator();
  
  int addInput(std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> input); // add a FIFO to be used as input
  
  void start(); // starts processing thread
  void stop(int waitStopped=1);  // stop processing thread (and possibly wait it terminates)


  static Thread::CallbackResult  threadCallback(void *arg);  
 
  private:
  std::vector<std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>> inputs;
  AliceO2::Common::Fifo<DataSetReference> *output;    //todo: unique_ptr
  
  std::unique_ptr<Thread> aggregateThread;
  AliceO2::Common::Timer incompletePendingTimer;
  int isIncompletePending;
};

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
        printf("1 block for event %llu from input %d @ %p\n",(unsigned long long)newId,b);
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

/* todo: shared_ptr for data pointers? */


// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x)/sizeof(x[0]) 

// function to convert a value in bytes to a prefixed number 3+3 digits
// suffix is the "base unit" to add after calculated prefix, e.g. Byte-> kBytes
std::string NumberOfBytesToString(double value,const char*suffix) {
  const char *prefixes[]={"","k","M","G","T","P"};
  int maxPrefixIndex=STATIC_ARRAY_ELEMENT_COUNT(prefixes)-1;
  int prefixIndex=log(value)/log(1024);
  if (prefixIndex>maxPrefixIndex) {
    prefixIndex=maxPrefixIndex;
  }
  if (prefixIndex<0) {
    prefixIndex=0;
  }
  double scaledValue=value/pow(1024,prefixIndex);
  char bufStr[64];
  if (suffix==nullptr) {
    suffix="";
  }
  snprintf(bufStr,sizeof(bufStr)-1,"%.03lf %s%s",scaledValue,prefixes[prefixIndex],suffix);
  return std::string(bufStr);  
}




// todo : replace DataBlockContainerReference by DataSetReference


class Consumer {
  public:
  Consumer(ConfigFile &cfg, std::string cfgEntryPoint) {
  };
  virtual ~Consumer() {
  };
  virtual int pushData(DataBlockContainerReference b)=0;
};

class ConsumerStats: public Consumer {
  private:
  unsigned long long counterBlocks;
  unsigned long long counterBytesTotal;
  unsigned long long counterBytesHeader;
  unsigned long long counterBytesDiff;
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer t;
  int monitoringEnabled;
  int monitoringUpdatePeriod;
//  std::unique_ptr<Collector> monitoringCollector;

  void publishStats() {
    if (monitoringEnabled) {
      // todo: support for long long types
      // https://alice.its.cern.ch/jira/browse/FLPPROT-69
 /*
      monitoringCollector->send(counterBlocks, "readout.Blocks");
      monitoringCollector->send(counterBytesTotal, "readout.BytesTotal");
      monitoringCollector->send(counterBytesDiff, "readout.BytesInterval");
//      monitoringCollector->send((counterBytesTotal/(1024*1024)), "readout.MegaBytesTotal");
*/
      counterBytesDiff=0;
    }
  }
  
  
  public: 
  ConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringEnabled", monitoringEnabled, 0);
    if (monitoringEnabled) {
      cfg.getOptionalValue(cfgEntryPoint + ".monitoringUpdatePeriod", monitoringUpdatePeriod, 10);
      const std::string configFile=cfg.getValue<std::string>(cfgEntryPoint + ".monitoringConfig");
      theLog.log("Monitoring enabled - period %ds - using configuration %s",monitoringUpdatePeriod,configFile.c_str());
/*
      monitoringCollector=MonitoringFactory::Create(configFile);
      monitoringCollector->addDerivedMetric("readout.BytesTotal", DerivedMetricMode::RATE);
*/
      t.reset(monitoringUpdatePeriod*1000000);
    }
    
    counterBytesTotal=0;
    counterBytesHeader=0;
    counterBlocks=0;
    counterBytesDiff=0;
    runningTime.reset();
  }
  ~ConsumerStats() {
    double elapsedTime=runningTime.getTime();
    if (counterBytesTotal>0) {
    theLog.log("Stats: %llu blocks, %.2f MB, %.2f%% header overhead",counterBlocks,counterBytesTotal/(1024*1024.0),counterBytesHeader*100.0/counterBytesTotal);
    theLog.log("Stats: average block size=%llu bytes",counterBytesTotal/counterBlocks);
    theLog.log("Stats: average throughput = %s",NumberOfBytesToString(counterBytesTotal/elapsedTime,"B/s").c_str());
    publishStats();
    } else {
      theLog.log("Stats: no data received");
    }
  }
  int pushData(DataBlockContainerReference b) {
    counterBlocks++;
    int newBytes=b->getData()->header.dataSize;
    counterBytesTotal+=newBytes;
    counterBytesDiff+=newBytes;
    counterBytesHeader+=b->getData()->header.headerSize;

    printf("Stats: got %p (%d)\n",b,b.use_count());
    if (monitoringEnabled) {
      // todo: do not check time every push() if it goes fast...      
      if (t.isTimeout()) {
        publishStats();
        t.increment();
      }
    }
    
    return 0;
  }
};




class ConsumerFileRecorder: public Consumer {
  public: 
  ConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    counterBytesTotal=0;
    fp=NULL;
    
    fileName=cfg.getValue<std::string>(cfgEntryPoint + ".fileName");
    if (fileName.length()>0) {
      theLog.log("Recording to %s",fileName.c_str());
      fp=fopen(fileName.c_str(),"wb");
      if (fp==NULL) {
        theLog.log("Failed to create file");
      }
    }
    if (fp==NULL) {
      theLog.log("Recording disabled");
    } else {
      theLog.log("Recording enabled");
    }   
  }  
  ~ConsumerFileRecorder() {
    closeRecordingFile();
  }
  int pushData(DataBlockContainerReference b) {

    int success=0;
    
    for(;;) {
      if (fp!=NULL) {
        void *ptr;
        size_t size;

        ptr=&b->getData()->header;
        size=b->getData()->header.headerSize;
        if (fwrite(ptr,size, 1, fp)!=1) {
          break;
        }     
        counterBytesTotal+=size;
        ptr=&b->getData()->data;
        size=b->getData()->header.dataSize; 
        if ((size>0)&&(ptr!=nullptr)) {
          if (fwrite(ptr,size, 1, fp)!=1) {
            break;
          }
        }
        counterBytesTotal+=size;
        success=1;
      }
      return 0;
    }    
    closeRecordingFile();
    return -1;
  }
  private:
    unsigned long long counterBytesTotal;
    FILE *fp;
    int recordingEnabled;
    std::string fileName;
    void closeRecordingFile() {
      if (fp!=NULL) {
        theLog.log("Closing %s",fileName.c_str());
        fclose(fp);
        fp=NULL;
      }
    }
};






class ConsumerDataSampling: public Consumer {
  public: 
  ConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
 
  }  
  ~ConsumerDataSampling() {
 
  }
  int pushData(DataBlockContainerReference b) {
    return 0;
  }
  private:
};













#ifdef WITH_FAIRMQ



class FMQSender : public FairMQDevice
{
  public:

    FMQSender() { }
    ~FMQSender() { }

  protected:   
    
    void Run() override {
       while (CheckCurrentState(RUNNING)) {
         //printf("loop Run()\n");
         usleep(200000);
       }
    }
};

class DataRef {
  public:
  std::shared_ptr<DataBlockContainer> ptr;
};

class ConsumerFMQ: public Consumer {
  private:
    std::vector<FairMQChannel> channels;
    FMQSender sender;


 // todo: check why this type is not public in FMQ interface?  
    typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;   
    FairMQMap m;
    
    FairMQTransportFactory *transportFactory;
        
  public: 

  static void CustomCleanup(void *data, void *object) {
    if ((object!=nullptr)&&(data!=nullptr)) {
      //printf("delete %p\n",object);
      delete ((DataRef *)object);
      //free(object);
    }
  }

  ConsumerFMQ(ConfigFile &cfg, std::string cfgEntryPoint) : Consumer(cfg,cfgEntryPoint), channels(1) {
       
    channels[0].UpdateType("pub");  // pub or push?
    channels[0].UpdateMethod("bind");
    channels[0].UpdateAddress("tcp://*:5555");
    channels[0].UpdateRateLogging(0);    
    channels[0].UpdateSndBufSize(10);    
    if (!channels[0].ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }


    // todo: def "data-out" as const string to name output channel to which we will push
    m.emplace(std::string("data-out"),channels);
    
    for (auto it : m) {
      std::cout << it.first << " = " << it.second.size() << " channels  " << std::endl;
      for (auto ch : it.second) {
        std::cout << ch.GetAddress() <<std::endl;
      }
    }
      
    sender.fChannels = m;
    transportFactory=new FairMQTransportFactoryZMQ();
    sender.SetTransport(transportFactory); // FairMQTransportFactory will be deleted when destroying sender
    sender.ChangeState(FairMQStateMachine::Event::INIT_DEVICE);
    sender.WaitForEndOfState(FairMQStateMachine::Event::INIT_DEVICE);
    sender.ChangeState(FairMQStateMachine::Event::INIT_TASK);
    sender.WaitForEndOfState(FairMQStateMachine::Event::INIT_TASK);
    sender.ChangeState(FairMQStateMachine::Event::RUN);

//    sender.InteractiveStateLoop();
  }
  
  ~ConsumerFMQ() {
    sender.ChangeState(FairMQStateMachine::Event::STOP);
    sender.ChangeState(FairMQStateMachine::Event::RESET_TASK);
    sender.WaitForEndOfState(FairMQStateMachine::Event::RESET_TASK);
    sender.ChangeState(FairMQStateMachine::Event::RESET_DEVICE);
    sender.WaitForEndOfState(FairMQStateMachine::Event::RESET_DEVICE);
    sender.ChangeState(FairMQStateMachine::Event::END);
  }
  
  int pushData(std::shared_ptr<DataBlockContainer>b) {

    DataRef *bCopy;
    bCopy=new DataRef;
    bCopy->ptr=b;

    /*void *p;
    p=malloc(b->getData()->header.dataSize);
    memcpy(p,b->getData()->data,b->getData()->header.dataSize);
    printf("sending %d @ %p\n",b->getData()->header.dataSize,p);    
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage(p, (size_t)(b->getData()->header.dataSize), ConsumerFMQ::CustomCleanup, (void *)(p)));
     sender.fChannels.at("data-out").at(0).Send(msgBody);
 */
 
    std::unique_ptr<FairMQMessage> msgHeader(transportFactory->CreateMessage((void *)&(b->getData()->header), (size_t)(b->getData()->header.headerSize), ConsumerFMQ::CustomCleanup, (void *)nullptr));
    std::unique_ptr<FairMQMessage> msgBody(transportFactory->CreateMessage((void *)(b->getData()->data), (size_t)(b->getData()->header.dataSize), ConsumerFMQ::CustomCleanup, (void *)(bCopy)));

    //printf("FMQ pushed data\n");

    sender.fChannels.at("data-out").at(0).Send(msgHeader);
    sender.fChannels.at("data-out").at(0).Send(msgBody);
    
    // how to know if it was a success?

    // every time we do a push there is a string compare ???
    //channels[0].SendPart(msgHeader);
    //channels[0].Send(msgBody);

//    channels.at("data-out").at(0).SendPart(msgBody);
    
    return 0;
  }
  private:
};

#endif

int main(int argc, char* argv[])
{
  ConfigFile cfg;
  const char* cfgFileURI="";
  if (argc<2) {
    printf("Please provide path to configuration file\n");
    return -1;
  }
  cfgFileURI=argv[1];

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings,sizeof(signalSettings));
  signalSettings.sa_handler=signalHandler;
  sigaction(SIGTERM,&signalSettings,NULL);
  sigaction(SIGQUIT,&signalSettings,NULL);
  sigaction(SIGINT,&signalSettings,NULL);

  // log startup and options
  theLog.log("Readout process starting");   
  theLog.log("Optional built features enabled:");
  #ifdef WITH_FAIRMQ
   theLog.log("FAIRMQ : yes");
  #else
   theLog.log("FAIRMQ : no");
  #endif

  // load configuration file
  theLog.log("Reading configuration from %s",cfgFileURI);  
  try {
    cfg.load(cfgFileURI);
  }
  catch (std::string err) {
    theLog.log("Error : %s",err.c_str());
    return -1;
  }
  
  // extract optional configuration parameters
  double cfgExitTimeout=-1;
  cfg.getOptionalValue<double>("readout.exitTimeout",cfgExitTimeout);


  // configure readout equipments
  std::vector<std::unique_ptr<ReadoutEquipment>> readoutDevices;
  for (auto kName : ConfigFileBrowser (&cfg,"equipment-")) {     

    // example iteration on each sub-key
    //for (auto kk : ConfigFileBrowser (&cfg,"",kName)) {
    //  printf("%s -> %s\n",kName.c_str(),kk.c_str());
    //}

    // skip disabled equipments
    int enabled=1;
    cfg.getOptionalValue<int>(kName + ".enabled",enabled);
    if (!enabled) {continue;}

    std::string cfgEquipmentType="";
    cfgEquipmentType=cfg.getValue<std::string>(kName + ".equipmentType");
    theLog.log("Configuring equipment %s: %s",kName.c_str(),cfgEquipmentType.c_str());
    
    std::unique_ptr<ReadoutEquipment>newDevice=nullptr;
    try {
      if (!cfgEquipmentType.compare("dummy")) {
        newDevice=std::make_unique<ReadoutEquipmentDummy>(cfg,kName);
      } else if (!cfgEquipmentType.compare("rorc")) {
        newDevice=std::make_unique<ReadoutEquipmentRORC>(cfg,kName);
      } else {
        theLog.log("Unknown equipment type '%s' for [%s]",cfgEquipmentType.c_str(),kName.c_str());
      }
    }
    catch (...) {
        theLog.log("Failed to configure equipment %s",kName.c_str());
        continue;
    }
    
    // add to list of equipments
    if (newDevice!=nullptr) {
      readoutDevices.push_back(std::move(newDevice));
    }   
  }


  // aggregator
  theLog.log("Creating aggregator");
  AliceO2::Common::Fifo<DataSetReference> agg_output(1000);
  int nEquipmentsAggregated=0;
  DataBlockAggregator agg(&agg_output,"Aggregator");
  for (auto && readoutDevice : readoutDevices) {
      //theLog.log("Adding equipment: %s",readoutDevice->getName().c_str());
      agg.addInput(readoutDevice->dataOut);
      nEquipmentsAggregated++;
  }
  theLog.log("Aggregator: %d equipments", nEquipmentsAggregated);


  // configuration of data sampling
  int dataSampling=0; 
  dataSampling=cfg.getValue<int>("sampling.enabled");
//  AliceO2::DataSampling::InjectorInterface *dataSamplingInjector = nullptr;
  if (dataSampling) {
    theLog.log("Data sampling enabled");
    // todo: create(...) should not need an argument and should get its configuration by itself.
//    dataSamplingInjector = AliceO2::DataSampling::InjectorFactory::create("FairInjector");
  } else {
    theLog.log("Data sampling disabled");
  }
  // todo: add time counter to measure how much time is spent waiting for data sampling injection (And other consumers)


  // configuration of data consumers
  std::vector<std::shared_ptr<Consumer>> dataConsumers;
  for (auto kName : ConfigFileBrowser (&cfg,"consumer-")) {

    // skip disabled
    int enabled=1;
    try {
      enabled=cfg.getValue<int>(kName + ".enabled");
    }
    catch (...) {
    }
    if (!enabled) {continue;}

    // instanciate consumer of appropriate type         
    std::shared_ptr<Consumer> newConsumer=nullptr;
    try {
      std::string cfgType="";
      cfgType=cfg.getValue<std::string>(kName + ".consumerType");
      theLog.log("Configuring consumer %s: %s",kName.c_str(),cfgType.c_str());
    
      if (!cfgType.compare("stats")) {
        newConsumer=std::make_shared<ConsumerStats>(cfg, kName);
      } else if (!cfgType.compare("FairMQDevice")) {
        #ifdef WITH_FAIRMQ
          newConsumer=std::make_shared<ConsumerFMQ>(cfg, kName);
        #else
          theLog.log("Skipping %s: %s - not supported by this build",kName.c_str(),cfgType.c_str());
        #endif
      } else if (!cfgType.compare("fileRecorder")) {
        newConsumer=std::make_shared<ConsumerFileRecorder>(cfg, kName);
      } else {
        theLog.log("Unknown consumer type '%s' for [%s]",cfgType.c_str(),kName.c_str());
      }
    } 
    catch (const std::exception& ex) {
        theLog.log("Failed to configure consumer %s : %s",kName.c_str(), ex.what());
        continue;
    } 
    catch (...) {
        theLog.log("Failed to configure consumer %s",kName.c_str());
        continue;
    }
        
    if (newConsumer!=nullptr) {
      dataConsumers.push_back(newConsumer);
    }
    
  }


  theLog.log("Starting aggregator");
  agg.start();
  
  theLog.log("Starting readout equipments");
  for (auto && readoutDevice : readoutDevices) {
      readoutDevice->start();
  }

  theLog.log("Running");

  // reset exit timeout, if any
  AliceO2::Common::Timer t;
  if (cfgExitTimeout>0) {
    t.reset(cfgExitTimeout*1000000);
    theLog.log("Automatic exit in %.2f seconds",cfgExitTimeout);
  }
  int isRunning=1;
  AliceO2::Common::Timer t0;
  t0.reset(); 


/*
  // reset stats
  unsigned long long nBlocks=0;
  unsigned long long nBytes=0;
  double t1=0.0;
*/


 theLog.log("Entering loop");

  while (1) {
    if (isRunning) {
      if (((cfgExitTimeout>0)&&(t.isTimeout()))||(ShutdownRequest)) {
        isRunning=0;
        theLog.log("Stopping readout");
        for (auto && readoutDevice : readoutDevices) {
          readoutDevice->stop();
        }
        theLog.log("Readout stopped");
        t.reset(1000000);  // add a delay before stopping aggregator - continune to empty FIFOs
      }
    } else {
      if (t.isTimeout()) {
        break;
      }
    }
    //DataBlockContainer *newBlock=NULL;
    //newBlock=r->getBlock();

    DataSetReference bc=nullptr;
    agg_output.pop(bc);    
    

    if (bc!=NULL) {
      // push to data sampling, if configured
/*      if (dataSampling && dataSamplingInjector) {
        dataSamplingInjector->injectSamples(*bc);
      }
  */
    
      unsigned int nb=(int)bc->size();
      //printf("received 1 vector made of %u blocks\n",nb);
      
      
      for (unsigned int i=0;i<nb;i++) {
/*
        printf("pop %d\n",i);
        printf("%p : %d use count\n",(void *)bc->at(i).get(), (int)bc->at(i).use_count());
*/
        std::shared_ptr<DataBlockContainer>b=bc->at(i);

/*
        nBlocks++;
        nBytes+=b->getData()->header.dataSize;
*/
//        printf("%p : %d use count\n",(void *)b.get(), (int)b.use_count());        
        
//        printf("pushed\n");
        for (auto c : dataConsumers) {
          c->pushData(b);
        }

       // todo: temporary - for the time being, delete done in FMQ. Replace by shared_ptr
//       delete b;    
//       b.reset();
       //printf("%p : %d use count\n",(void *)b.get(), b.use_count());
        //printf("pop %p\n",(void *)b);

      }
      // todo: check if following needed or not... in principle not as it is a shared_ptr
      // delete bc;
    } else {
      usleep(1000);
    }

  }

  theLog.log("Stopping aggregator");
  agg.stop();


//  t1=t0.getTime();
  
  theLog.log("Wait a bit");
  sleep(1);
  theLog.log("Stop consumers");
  
  // close consumers before closing readout equipments (owner of data blocks)
  dataConsumers.clear();

  agg_output.clear();
  
  // todo: check nothing in the input pipeline
  // flush & stop equipments
  for (auto && readoutDevice : readoutDevices) {
      // ensure nothing left in output FIFO to allow releasing memory
//      printf("readout: in=%llu  out=%llu\n",readoutDevice->dataOut->getNumberIn(),readoutDevice->dataOut->getNumberOut());      
      readoutDevice->dataOut->clear();
  }


//  printf("agg: in=%llu  out=%llu\n",agg_output.getNumberIn(),agg_output.getNumberOut());

  theLog.log("Closing readout devices");
  for (size_t i = 0, size = readoutDevices.size(); i != size; ++i) {
    readoutDevices[i]=nullptr;  // effectively deletes the device
  }
  readoutDevices.clear(); // to do it all in one go
/*
  if(dataSamplingInjector) {
    delete dataSamplingInjector;
  }
*/

/*
  theLog.log("%llu blocks in %.3lf seconds => %.1lf block/s",nBlocks,t1,nBlocks/t1);
  theLog.log("%.1lf MB received",nBytes/(1024.0*1024.0));
  theLog.log("%.3lf MB/s",nBytes/(1024.0*1024.0)/t1);
*/

  theLog.log("Operations completed");

  return 0;

}
