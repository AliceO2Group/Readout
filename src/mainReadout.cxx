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

#include <DataSampling/InjectSamples.h>
  
#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

#include "RORC/Parameters.h"
#include "RORC/ChannelFactory.h"


using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;
  

InfoLogger theLog;
  
  
Thread::CallbackResult  testloop(void *arg) {
  printf("testloop ( %p)...\n",arg);
  sleep(1);
  return Thread::CallbackResult::Ok;
}


static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int){
  ShutdownRequest=1;
}

class CReadout {
  public:
  CReadout(ConfigFile *cfg, std::string name="readout");
  virtual ~CReadout();
  
  DataBlockContainer *getBlock();

  void start();
  void stop();
  const std::string & getName();

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
  protected:
  std::string name;
};

CReadout::CReadout(ConfigFile *cfg, std::string _name) {
  // todo: take thread name from config, or as argument
  
  name=_name;
  
  readoutThread=new Thread(CReadout::threadCallback,this,name,1000);
  //readoutThread->start();
  int outFifoSize=1000;
  dataOut=new AliceO2::Common::Fifo<DataBlockContainer>(outFifoSize);
  
  readoutRate=-1; //(target readout rate in Hz, -1 for unlimited)
  try {
    readoutRate=cfg->getValue<double>("readout.rate");
  }
  catch(std::string e) {
    theLog.log("Configuration error: %s",e.c_str());
  }

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
  mp=new MemPool(1000,0.01*1024*1024);
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
  catch (...) {
    return Thread::CallbackResult::Idle;
  }
  //printf("push %p\n",(void *)d);
  
  if (d==NULL) {
    return Thread::CallbackResult::Idle;
  }
  
  DataBlock *b=d->getData();
  
  int dSize=(int)(1024+rand()*1024.0/RAND_MAX);
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
    CReadoutRORC(ConfigFile *cfg, std::string name="rorcReadout");
    ~CReadoutRORC();
  
  private:
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    AliceO2::Rorc::ChannelFactory::MasterSharedPtr channel;
    int pageCount=0;
    int isInitialized=0;
};



CReadoutRORC::CReadoutRORC(ConfigFile *cfg, std::string name) : CReadout(cfg, name) {
  
  try {

    int serialNumber=cfg->getValue<int>(name + ".serial");
    int channelNumber=cfg->getValue<int>(name + ".channel");
  
    theLog.log("Opening RORC %d:%d",serialNumber,channelNumber);

    //AliceO2::Rorc::ChannelFactory::DUMMY_SERIAL_NUMBER; //pcaldref23: 33333

    AliceO2::Rorc::Parameters::Map params;
    params[AliceO2::Rorc::Parameters::Keys::dmaBufferSize()]=std::to_string(32*1024*1024);
    params[AliceO2::Rorc::Parameters::Keys::dmaPageSize()]=std::to_string(8*1024);
    params[AliceO2::Rorc::Parameters::Keys::generatorDataSize()]=std::to_string(8*1024);
    params[AliceO2::Rorc::Parameters::Keys::generatorEnabled()]=std::to_string(true);

    channel = AliceO2::Rorc::ChannelFactory().getMaster(serialNumber, channelNumber, params);

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
      DataBlockContainerFromRORC *d=nullptr;
      try {
        d=new DataBlockContainerFromRORC(channel);
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
  
  std::vector<DataBlockContainer *> *bcv=nullptr;
  try {
    bcv=new std::vector<DataBlockContainer *>();
  }
  catch(...) {
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



  ConfigFile cfg;


  const char* cfgFile="";
  if (argc<2) {
    printf("Please provide path to configuration file\n");
    return -1;
  }
  cfgFile=argv[1];
  theLog.log("Readout process starting");
  theLog.log("Reading configuration from %s",cfgFile);  



  try {
    cfg.load(cfgFile);
   }
  catch (std::string err) {
    theLog.log("Error : %s",err.c_str());
    return -1;
  }

  double cfgExitTimeout=-1;
  try {
    cfgExitTimeout=cfg.getValue<double>("readout.exitTimeout");
  }
  catch(std::string e) {
  }



  std::vector<CReadout*> readoutDevices;

  for (auto kName : ConfigFileBrowser (&cfg,"equipment-")) {     

    int enabled=1;
    try {
      enabled=cfg.getValue<int>(kName + ".enabled");
    }
    catch (...) {
    }
    // skip disabled equipments
    if (!enabled) {continue;}

    std::string cfgEquipmentType="";
    cfgEquipmentType=cfg.getValue<std::string>(kName + ".equipmentType");

    theLog.log("Configuring equipment %s: %s",kName.c_str(),cfgEquipmentType.c_str());
    
    CReadout *newDevice=nullptr;
    try {
      if (!cfgEquipmentType.compare("dummy")) {
      // todo: how to pass extra params: rate, size, etc. Handle to config subsection?
        newDevice=new CReadoutDummy(&cfg,kName);
      } else if (!cfgEquipmentType.compare("rorc")) {
        newDevice=new CReadoutRORC(&cfg,kName);
      } else {
        theLog.log("Unknown equipment type '%s' for [%s]",cfgEquipmentType.c_str(),kName.c_str());
      }
    }
    catch (...) {
        theLog.log("Failed to configure equipment %s",kName.c_str());
        continue;
    }
        
    if (newDevice!=nullptr) {
      readoutDevices.push_back(newDevice);
    }
    
  }

  AliceO2::Common::Fifo<std::vector<DataBlockContainer *>> agg_output(1000);  
  CAggregator agg(&agg_output,"Aggregator");

  theLog.log("Creating aggregator");
  for (auto readoutDevice : readoutDevices) {
      theLog.log("Adding equipment: %s",readoutDevice->getName().c_str());
      agg.addInput(readoutDevice->dataOut);
  }


  // configuration of data recording

  int recordingEnabled=0;
  std::string recordingFile="";
  
  recordingEnabled=cfg.getValue<int>("recording.enabled");
  recordingFile=cfg.getValue<std::string>("recording.fileName");
  
  FILE *fp=NULL;
  if (recordingEnabled) {
    if (recordingFile.length()>0) {
      theLog.log("Recording to %s",recordingFile.c_str());
      fp=fopen(recordingFile.c_str(),"wb");
      if (fp==NULL) {
        theLog.log("Failed to create file");
        recordingEnabled=0;
      }
    }
  }

  // configuration of data sampling

  int dataSampling=0; 
  dataSampling=cfg.getValue<int>("sampling.enabled");
  
  if (dataSampling) {
    theLog.log("Data sampling enabled");
  } else {
    theLog.log("Data sampling disabled");
  }
  // todo: add time counter to measure how much time is spent waiting for data sampling injection
  


  theLog.log("Starting aggregator");
  agg.start();
  
    theLog.log("Starting readout");
  for (auto readoutDevice : readoutDevices) {
      readoutDevice->start();
  }

  theLog.log("Running");


  AliceO2::Common::Timer t;
  if (cfgExitTimeout>0) {
    t.reset(cfgExitTimeout*1000000);
    theLog.log("Automatic exit in %.2f seconds",cfgExitTimeout);
  }
  int isRunning=1;
  //r->start();
  AliceO2::Common::Timer t0;
  t0.reset();
  unsigned long long nBlocks=0;
  unsigned long long nBytes=0;
  double t1=0.0;

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings,sizeof(signalSettings));
  signalSettings.sa_handler=signalHandler;
  sigaction(SIGTERM,&signalSettings,NULL);
  sigaction(SIGQUIT,&signalSettings,NULL);
  sigaction(SIGINT,&signalSettings,NULL);


  while (1) {
    if (((cfgExitTimeout>0)&&(t.isTimeout()))||(ShutdownRequest)) {
      if (isRunning) {
        isRunning=0;
        theLog.log("Stopping readout");
        for (auto readoutDevice : readoutDevices) {
          readoutDevice->stop();
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
    
    
      // push to data sampling, if configured
      if (dataSampling) {
        injectSamples(*bc);
      }
    
    
      unsigned int nb=(int)bc->size();
      //printf("received 1 vector made of %u blocks\n",nb);
      
      
      for (unsigned int i=0;i<nb;i++) {
        //printf("pop %d\n",i);
        DataBlockContainer *b=bc->at(i);

        nBlocks++;
        nBytes+=b->getData()->header.dataSize;


        // recording, if configured
        
        if (fp!=NULL) {
          void *ptr;
          size_t size;
          
          ptr=&b->getData()->header;
          size=b->getData()->header.headerSize;
          fwrite(ptr,size, 1, fp);
          
          //printf("WRITE: header @ %p - %d\n",ptr,(int)size);

          ptr=&b->getData()->data;
          size=b->getData()->header.dataSize;          
          fwrite(ptr,size, 1, fp);
      
      // todo: file rec: add header to vector of blocks               
          //printf("WRITE: data @ %p - %d\n",ptr,(int)size);

          /*
          fwrite(&b->getData()->header, b->getData()->header.headerSize, 1, fp);
          if (b->getData()->data!=NULL) {
            fwrite(b->getData()->data, b->getData()->header.dataSize, 1, fp);
          }
          */
        }

        delete b;
        //printf("pop %p\n",(void *)b);

      }
      delete bc;      
    } else {
      usleep(1000);
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

  theLog.log("Stopping aggregator");
  agg.stop();

  
  if (fp!=NULL) {
    theLog.log("Closing %s",recordingFile.c_str());
    fclose(fp);
  }


  for (auto readoutDevice : readoutDevices) {
      delete readoutDevice;
  }

  theLog.log("%llu blocks in %.3lf seconds => %.1lf block/s",nBlocks,t1,nBlocks/t1);
  theLog.log("%.1lf MB received",nBytes/(1024.0*1024.0));
  theLog.log("%.3lf MB/s",nBytes/(1024.0*1024.0)/t1);


  theLog.log("Operations completed");

  return 0;

}
