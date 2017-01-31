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
#include <signal.h>

#include <memory>

#include <DataSampling/InjectSamples.h>
  
#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

#include "RORC/Parameters.h"
#include "RORC/ChannelFactory.h"

#ifdef WITH_FAIRMQ
#include <FairMQDevice.h>
#include <FairMQMessage.h>
#include <FairMQTransportFactory.h>
#include <FairMQTransportFactoryZMQ.h>
#include <FairMQProgOptions.h>
#endif

using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;
  
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



class CReadout {
  public:
  CReadout(ConfigFile &cfg, std::string cfgEntryPoint);
  virtual ~CReadout();
  
  std::shared_ptr<DataBlockContainer>getBlock();

  void start();
  void stop();
  const std::string & getName();

//  protected: 
// todo: give direct access to output FIFO?
  AliceO2::Common::Fifo<std::shared_ptr<DataBlockContainer>> *dataOut;

  private:
  Thread *readoutThread;  
  static Thread::CallbackResult  threadCallback(void *arg);
  virtual Thread::CallbackResult  populateFifoOut()=0;  // function called iteratively in dedicated thread to populate FIFO
  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;
  
  unsigned long long nBlocksOut;
  double readoutRate;
  protected:
  std::string name;
};


CReadout::CReadout(ConfigFile &cfg, std::string cfgEntryPoint) {
  
  // example: browse config keys
  //for (auto cfgKey : ConfigFileBrowser (&cfg,"",cfgEntryPoint)) {
  //  std::string cfgValue=cfg.getValue<std::string>(cfgEntryPoint + "." + cfgKey);
  //  printf("%s.%s = %s\n",cfgEntryPoint.c_str(),cfgKey.c_str(),cfgValue.c_str());
  //}

  
  // by default, name the equipment as the config node entry point
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".name", name, cfgEntryPoint);

  // target readout rate in Hz, -1 for unlimited (default)
  cfg.getOptionalValue<double>("readout.rate",readoutRate,-1.0);


  readoutThread=new Thread(CReadout::threadCallback,this,name,1000);

  int outFifoSize=1000;
  
  dataOut=new AliceO2::Common::Fifo<std::shared_ptr<DataBlockContainer>>(outFifoSize);
  nBlocksOut=0;
}

const std::string & CReadout::getName() {
  return name;
}

void CReadout::start() {
  readoutThread->start();
  if (readoutRate>0) {
    clk.reset(1000000.0/readoutRate);
  }
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

std::shared_ptr<DataBlockContainer> CReadout::getBlock() {
  std::shared_ptr<DataBlockContainer>b=nullptr;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult  CReadout::threadCallback(void *arg) {
  CReadout *ptr=(CReadout *)arg;
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






class CReadoutDummy : public CReadout {

  public:
    CReadoutDummy(ConfigFile &cfg, std::string name="dummyReadout");
    ~CReadoutDummy();
  
  private:
    MemPool *mp;
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    int eventMaxSize;
    int eventMinSize;    
};


CReadoutDummy::CReadoutDummy(ConfigFile &cfg, std::string cfgEntryPoint) : CReadout(cfg, cfgEntryPoint) {

  int memPoolNumberOfElements=10000;
  int memPoolElementSize=0.01*1024*1024;

  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolNumberOfElements", memPoolNumberOfElements);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolElementSize", memPoolElementSize);

  mp=new MemPool(memPoolNumberOfElements,memPoolElementSize);
  currentId=0;
  
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMaxSize", eventMaxSize, (int)1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMinSize", eventMinSize, (int)1024);
}

CReadoutDummy::~CReadoutDummy() {
  delete mp;
} 

Thread::CallbackResult  CReadoutDummy::populateFifoOut() {
  if (dataOut->isFull()) {
    return Thread::CallbackResult::Idle;
  }

  std::shared_ptr<DataBlockContainer>d=NULL;
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
  
  //printf("(2)header=%p\nbase=%p\nsize=%d,%d\n",(void *)&(b->header),b->data,(int)b->header.headerSize,(int)b->header.dataSize);
    
  //usleep(10000);
  
  // push new page to mem
  dataOut->push(d); 

  //printf("populateFifoOut()\n");
  return Thread::CallbackResult::Ok;
}











class DataBlockContainerFromRORC : public DataBlockContainer {
  private:
  AliceO2::Rorc::ChannelMasterInterface::PageSharedPtr pagePtr;
   
  public:
  DataBlockContainerFromRORC(AliceO2::Rorc::ChannelFactory::MasterSharedPtr v_channel) {
    data=nullptr;
    

    
    
    pagePtr=AliceO2::Rorc::ChannelMasterInterface::popPage(v_channel);
    
    if (pagePtr!=nullptr) {
    
//      printf("got page: %p -> evId=%d\n",(*v_page).getAddress(),*((*v_page).getAddressU32()));
//      v_channel->freePage(v_page);    
//      return;
      DataBlock *v_data=nullptr;
      try {
         v_data=new DataBlock;
      } catch (...) {
        throw __LINE__;
      }
      data=v_data;
      data->header.blockType=DataBlockType::H_BASE;
      data->header.headerSize=sizeof(DataBlockHeaderBase);
      data->header.dataSize=8*1024;
      data->header.id=*(pagePtr->getAddressU32());
      data->data=(char *)(pagePtr->getAddress());

//      pagePtr.reset();

//      pageIndexLog[page.index]=1;

      //printf("page new = %p %d\n",page.getAddress(),page.index);

//      printf("got page: %p -> evId=%d\n",(*v_page).getAddress(),*((*v_page).getAddressU32()));
//      usleep(1000000);
 

//      printf("page = %p\n",&page);
//      printf("got page data: %p -> evId=%d\n",data->data,(int)data->header.id);
//      usleep(1000000);
    } else {
      printf("No new page available!");
      throw __LINE__;
    }
  }
  ~DataBlockContainerFromRORC() {
//    delete data;

//    channel->freePage(page);    
    if (data!=nullptr) {
      delete data;
      //printf("page delete=%p %d\n",page.getAddress(),page.index);
      //fflush(stdout);
/*      if (pageIndexLog[page.index]==0) {
        printf("error - index not used\n");
      }
      */
      try {
        pagePtr.reset();
//        pageIndexLog[page.index]=0;
        //channel->freePage(page);  
      }
      catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e, 1) << "\n";
      }

    }
  }
};








class CReadoutRORC : public CReadout {

  public:
    CReadoutRORC(ConfigFile &cfg, std::string name="rorcReadout");
    ~CReadoutRORC();
  
  private:
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    AliceO2::Rorc::ChannelFactory::MasterSharedPtr channel;
    int pageCount=0;
    int isInitialized=0;
};



CReadoutRORC::CReadoutRORC(ConfigFile &cfg, std::string name) : CReadout(cfg, name) {
  
  try {

    int serialNumber=cfg.getValue<int>(name + ".serial");
    int channelNumber=cfg.getValue<int>(name + ".channel");
  
    theLog.log("Opening RORC %d:%d",serialNumber,channelNumber);

    //AliceO2::Rorc::ChannelFactory::DUMMY_SERIAL_NUMBER; //pcaldref23: 33333

    AliceO2::Rorc::Parameters params = AliceO2::Rorc::Parameters::makeParameters(serialNumber,channelNumber)
        .setDmaBufferSize(32*1024*1024)
        .setDmaPageSize(8*1024)
        .setGeneratorDataSize(8*1024)
        .setGeneratorEnabled(true);

    channel = AliceO2::Rorc::ChannelFactory().getMaster(params);

    //channel->resetCard(AliceO2::Rorc::ResetLevel::Rorc);
    channel->startDma();


  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e, 1) << "\n";
    return;
  }
  isInitialized=1;
}



CReadoutRORC::~CReadoutRORC() {
  if (isInitialized) {
    channel->stopDma();
  }

  theLog.log("Equipment %s : %d pages read",name.c_str(),(int)pageCount);
//  printf("count: %d\n",(int)channel.use_count());
}
/*
void processChannel(AliceO2::Rorc::ChannelFactory::MasterSharedPtr channel) {
  if (boost::optional<AliceO2::Rorc::ChannelMasterInterface::Page> page = channel->getPage()) {    
   int eventId=0;
   eventId=*((*page).getAddressU32());
    printf("ev=%d\n",eventId);
      channel->freePage(page);
  }
}
*/

Thread::CallbackResult  CReadoutRORC::populateFifoOut() {
  if (!isInitialized) return  Thread::CallbackResult::Error;
  
  channel->fillFifo();
 
  if (dataOut->isFull()) {
    return Thread::CallbackResult::Idle;
  }

//  processChannel(channel);
//  return Thread::CallbackResult::Idle;
  
/*  if (boost::optional<AliceO2::Rorc::ChannelMasterInterface::Page> page = channel->getPage()) {    
   int eventId=0;
   eventId=*((*page).getAddressU32());
    printf("%s : ev=%d\n",name.c_str(),eventId);
      channel->freePage(page);
  }
  return Thread::CallbackResult::Idle;
*/  
      
    int nPagesAvailable=channel->getAvailableCount();
    if (!nPagesAvailable) {
      return Thread::CallbackResult::Idle;
    }
    
    for (int i=0;i<nPagesAvailable;i++) {
      std::shared_ptr<DataBlockContainerFromRORC>d=nullptr;
      try {
        d=std::make_shared<DataBlockContainerFromRORC>(channel);
      }
      catch (...) {
        return Thread::CallbackResult::Idle;
      }
      dataOut->push(d); 
      pageCount++;
      
      break;
      // todo: if looping, check status of throttle, or receive as parameter max number of fifo slots to populate
    }
    
    return Thread::CallbackResult::Ok;
}







class CAggregator {
  public:
  CAggregator(AliceO2::Common::Fifo<std::vector<std::shared_ptr<DataBlockContainer>>*> *output, std::string name="Aggregator");
  ~CAggregator();
  
  int addInput(AliceO2::Common::Fifo<std::shared_ptr<DataBlockContainer>> *input); // add a FIFO to be used as input
  
  void start(); // starts processing thread
  void stop(int waitStopped=1);  // stop processing thread (and possibly wait it terminates)


  static Thread::CallbackResult  threadCallback(void *arg);  
 
  private:
  std::vector<AliceO2::Common::Fifo<std::shared_ptr<DataBlockContainer>> *> inputs;
  AliceO2::Common::Fifo<std::vector<std::shared_ptr<DataBlockContainer>> *> *output;
  
  Thread *aggregateThread;
  AliceO2::Common::Timer incompletePendingTimer;
  int isIncompletePending;
};

CAggregator::CAggregator(AliceO2::Common::Fifo<std::vector<std::shared_ptr<DataBlockContainer>> *> *v_output, std::string name){
  output=v_output;
  aggregateThread=new Thread(CAggregator::threadCallback,this,name,100);
  isIncompletePending=0;
}

CAggregator::~CAggregator() {
  // todo: flush out FIFOs ?
  delete aggregateThread;
}
  
int CAggregator::addInput(AliceO2::Common::Fifo<std::shared_ptr<DataBlockContainer>> *input) {
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
      std::shared_ptr<DataBlockContainer>bc=nullptr;
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
  
  std::vector<std::shared_ptr<DataBlockContainer>> *bcv=nullptr;
  try {
    bcv=new std::vector<std::shared_ptr<DataBlockContainer>>();
  }
  catch(...) {
    return Thread::CallbackResult::Error;
  }
  
  
  for (unsigned int i=0; i<dPtr->inputs.size(); i++) {
    if (!dPtr->inputs[i]->isEmpty()) {    
      std::shared_ptr<DataBlockContainer>b=nullptr;
      dPtr->inputs[i]->front(b);
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
  //printf("agg: new block\n");
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

/* todo: shared_ptr for data pointers? */



class Consumer {
  public:
  Consumer(ConfigFile &cfg, std::string cfgEntryPoint) {
  };
  virtual ~Consumer() {
  };
  virtual int pushData(std::shared_ptr<DataBlockContainer>b)=0;
};

class ConsumerStats: public Consumer {
  public: 
  ConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    counterBytesTotal=0;
    counterBytesHeader=0;
    counterBlocks=0;
  }
  ~ConsumerStats() {
    theLog.log("Stats: %llu blocks, %.2f MB, %.2f%% header overhead",counterBlocks,counterBytesTotal/(1024*1024.0),counterBytesHeader*100.0/counterBytesTotal);
    theLog.log("Stats: average block size=%llu bytes",counterBytesTotal/counterBlocks);
  }
  int pushData(std::shared_ptr<DataBlockContainer>b) {
    counterBlocks++;
    counterBytesTotal+=b->getData()->header.dataSize;
    counterBytesHeader+=b->getData()->header.headerSize;
    return 0;
  }
  private:
    unsigned long long counterBlocks;
    unsigned long long counterBytesTotal;
    unsigned long long counterBytesHeader;
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
  int pushData(std::shared_ptr<DataBlockContainer>b) {

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
        if (fwrite(ptr,size, 1, fp)!=1) {
          break;
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
  int pushData(std::shared_ptr<DataBlockContainer>b) {
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
  std::vector<CReadout*> readoutDevices;
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
    
    CReadout *newDevice=nullptr;
    try {
      if (!cfgEquipmentType.compare("dummy")) {
        newDevice=new CReadoutDummy(cfg,kName);
      } else if (!cfgEquipmentType.compare("rorc")) {
        newDevice=new CReadoutRORC(cfg,kName);
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
      readoutDevices.push_back(newDevice);
    }   
  }


  // aggregator
  theLog.log("Creating aggregator");
  AliceO2::Common::Fifo<std::vector<std::shared_ptr<DataBlockContainer>> *> agg_output(1000);
  int nEquipmentsAggregated=0;
  CAggregator agg(&agg_output,"Aggregator");
  for (auto readoutDevice : readoutDevices) {
      //theLog.log("Adding equipment: %s",readoutDevice->getName().c_str());
      agg.addInput(readoutDevice->dataOut);
      nEquipmentsAggregated++;
  }
  theLog.log("Aggregator: %d equipments", nEquipmentsAggregated);


  // configuration of data sampling
  int dataSampling=0; 
  dataSampling=cfg.getValue<int>("sampling.enabled");
  if (dataSampling) {
    theLog.log("Data sampling enabled");
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
  for (auto readoutDevice : readoutDevices) {
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
  
  // reset stats
  unsigned long long nBlocks=0;
  unsigned long long nBytes=0;
  double t1=0.0;


 theLog.log("Entering loop");

  while (1) {
    if (isRunning) {
      if (((cfgExitTimeout>0)&&(t.isTimeout()))||(ShutdownRequest)) {
        isRunning=0;
        theLog.log("Stopping readout");
        for (auto readoutDevice : readoutDevices) {
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

    std::vector<std::shared_ptr<DataBlockContainer>> *bc=NULL;
    agg_output.pop(bc);    
    

    if (bc!=NULL) {
    
    
      // push to data sampling, if configured
      if (dataSampling) {
        injectSamples(*bc);
      }
    
    
      unsigned int nb=(int)bc->size();
      //printf("received 1 vector made of %u blocks\n",nb);
      
      
      for (unsigned int i=0;i<nb;i++) {
        //printf("pop %d\n",i);
        //printf("%p : %d use count\n",(void *)bc->at(i).get(), bc->at(i).use_count());
        std::shared_ptr<DataBlockContainer>b=bc->at(i);

        nBlocks++;
        nBytes+=b->getData()->header.dataSize;
        //printf("%p : %d use count\n",(void *)b.get(), b.use_count());
        
        
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
      delete bc;      
    } else {
      usleep(1000);
    }

  }

  theLog.log("Stopping aggregator");
  agg.stop();

  theLog.log("Wait a bit");
  sleep(1);
  theLog.log("Stop consumers");
  
  // close consumers before closing readout equipments (owner of data blocks)
  dataConsumers.clear();

  return 0;
  
  
  // todo: check nothing in the input pipeline
  // flush & stop equipments
  
  theLog.log("Closing readout devices");  
  for (auto readoutDevice : readoutDevices) {
      delete readoutDevice;
  }

  theLog.log("%llu blocks in %.3lf seconds => %.1lf block/s",nBlocks,t1,nBlocks/t1);
  theLog.log("%.1lf MB received",nBytes/(1024.0*1024.0));
  theLog.log("%.3lf MB/s",nBytes/(1024.0*1024.0)/t1);


  theLog.log("Operations completed");

  return 0;

}
