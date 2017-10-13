#include "ReadoutEquipment.h"

#include <ReadoutCard/Parameters.h>
#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>

#include <string>


#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;


namespace ReadoutUtils {

// function to convert a string to a 64-bit integer value
// allowing usual "base units" in suffix (k,M,G,T)
// input can be decimal (1.5M is valid, will give 1.5*1024*1024)
long long getNumberOfBytesFromString(const char * inputString) {
  double v=0;
  char c='?';
  int n=sscanf(inputString,"%lf%c",&v,&c);
  
  if (n==1) {
    return (long long)v;
  }
  if (n==2) {
    if (c=='k') {
      return (long long)(v*1024LL);
    }
    if (c=='M') {
      return (long long)(v*1024LL*1024LL);
    }
    if (c=='G') {
      return (long long)(v*1024LL*1024LL*1024LL);
    }
    if (c=='T') {
      return (long long)(v*1024LL*1024LL*1024LL*1024LL);
    }    
    if (c=='P') {
      return (long long)(v*1024LL*1024LL*1024LL*1024LL*1024LL);
    }    
  }
  return 0;
}


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


}
  
// a big block of memory for I/O
class ReadoutMemoryHandler {
  public:
  size_t memorySize;  // total size of buffer
  size_t pageSize;       // size of each superpage in buffer (not the one of getpagesize())
  uint8_t * baseAddress; // base address of buffer

  std::unique_ptr<AliceO2::Common::Fifo<long>> pagesAvailable;  // a buffer to keep track of individual pages. storing offset (with respect to base address) of pages available
  
  private:
  std::unique_ptr<AliceO2::roc::MemoryMappedFile> mMemoryMappedFile;
  
  public:
  
  ReadoutMemoryHandler(size_t vMemorySize, int vPageSize, std::string const &uid){
  
    pagesAvailable=nullptr;
    mMemoryMappedFile=nullptr;
    
    // path to our memory segment
    std::string memoryMapBasePath="/var/lib/hugetlbfs/global/pagesize-1GB/";
    std::string memoryMapFilePath=memoryMapBasePath + uid;
      
    // must be multiple of hugepage size
    const int hugePageSize=1024*1024*1024;
    int r=vMemorySize % hugePageSize;
    if (r) {
      vMemorySize+=hugePageSize-r;
    }

    theLog.log("Creating shared memory block - %s @ %s",ReadoutUtils::NumberOfBytesToString(vMemorySize,"Bytes").c_str(),memoryMapFilePath.c_str());
    
    try {
      mMemoryMappedFile=std::make_unique<AliceO2::roc::MemoryMappedFile>(memoryMapFilePath,vMemorySize,false);
    }
    catch (const AliceO2::roc::MemoryMapException& e) {
      theLog.log("Failed to allocate memory buffer : %s\n",e.what());
      exit(1);
    }
    // todo: check consistent with what requested, alignment, etc
    memorySize=mMemoryMappedFile->getSize();
    baseAddress=(uint8_t *)mMemoryMappedFile->getAddress();

    int baseAlignment=1024*1024;    
    r=(long)baseAddress % baseAlignment;
    if (r!=0) {
      theLog.log("Unaligned base address %p, target alignment=%d, skipping first %d bytes",baseAddress,baseAlignment,baseAlignment-r);
      int skipBytes=baseAlignment-r;
      baseAddress+=skipBytes;
      memorySize-=skipBytes;
      // now baseAddress is aligned to baseAlignment bytes, but what guarantee do we have that PHYSICAL page addresses are aligned ???
    }
    
    pageSize=vPageSize;
    long long nPages=memorySize/pageSize;
    theLog.log("Got %lld pages, each %s",nPages,ReadoutUtils::NumberOfBytesToString(pageSize,"Bytes").c_str());
    pagesAvailable=std::make_unique<AliceO2::Common::Fifo<long>>(nPages);
    
    for (int i=0;i<nPages;i++) {
      long offset=i*pageSize;
      //void *page=&((uint8_t*)baseAddress)[offset];
      //printf("%d : 0x%p\n",i,page);
      pagesAvailable->push(offset);
    }
    
  }
  ~ReadoutMemoryHandler() {
  }
 
};
// todo: locks for thread-safe access at init time
//ReadoutMemoryHandler mReadoutMemoryHandler;




class DataBlockContainerFromRORC : public DataBlockContainer {
  private:
  AliceO2::roc::ChannelFactory::DmaChannelSharedPtr mChannel;
  AliceO2::roc::Superpage mSuperpage;
  std::shared_ptr<ReadoutMemoryHandler> mReadoutMemoryHandler;  // todo: store this in superpage user data
  
  public:
  DataBlockContainerFromRORC(AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel, AliceO2::roc::Superpage  const & superpage, std::shared_ptr<ReadoutMemoryHandler> const & h) {

    mSuperpage=superpage;
    mChannel=channel;
    mReadoutMemoryHandler=h;

    data=nullptr;
    try {
       data=new DataBlock;
    } catch (...) {
      throw __LINE__;
    }   


    data->header.blockType=DataBlockType::H_BASE;
    data->header.headerSize=sizeof(DataBlockHeaderBase);
    data->header.dataSize=superpage.getReceived();
    
    uint32_t *ptr=(uint32_t *) & (((uint8_t *)h->baseAddress)[mSuperpage.getOffset()]);
    
    data->header.id=*ptr;
    data->data=(char *)ptr;
    
/*    for (int i=0;i<64;i++) {
      printf("%08X  ",(((unsigned int *)ptr)[i]));
    }
    printf("\n");
    sleep(1);
*/
    
    //printf("container %p created for superpage @ offset %ld = %p\n",this,(long)superpage.getOffset(),ptr);    
  }
  
  ~DataBlockContainerFromRORC() {
    // todo: add lock
    // if constructor fails, do we make page available again or leave it to caller?
    mReadoutMemoryHandler->pagesAvailable->push(mSuperpage.getOffset());
    
    //printf("released superpage %ld\n",mSuperpage.getOffset());
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
    Thread::CallbackResult  populateFifoOut(); // the data readout loop function
    
    AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel;    // channel to ROC device
    std::shared_ptr<ReadoutMemoryHandler> mReadoutMemoryHandler;  // object to get memory from

    DataBlockId currentId=0;    // current data id, kept for auto-increment
       
    int isInitialized=0;        // flag set to 1 when class has been successfully initialized

    int pageCount=0;                // number of pages read
    unsigned long long loopCount=0; // number of populateFifo loop counts 

    int RocFifoSize=0;  // detected size of ROC fifo (when filling it for the first time)
};



ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name) : ReadoutEquipment(cfg, name) {

  loopCount=0;
    
  try {

    // get parameters from configuration
    // config keys are the same as the corresponding set functions in AliceO2::roc::Parameters
    
    std::string cardId=cfg.getValue<std::string>(name + ".cardId");
    
    int cfgChannelNumber=0;
    cfg.getOptionalValue<int>(name + ".channelNumber", cfgChannelNumber);

    int cfgGeneratorEnabled=0;
    cfg.getOptionalValue<int>(name + ".generatorEnabled", cfgGeneratorEnabled);
    
    int cfgGeneratorDataSize=8192;
    cfg.getOptionalValue<int>(name + ".generatorDataSize", cfgGeneratorDataSize);
    
    std::string cfgGeneratorLoopback="NONE";
    cfg.getOptionalValue<std::string>(name + ".generatorLoopback", cfgGeneratorLoopback);

    std::string cfgGeneratorPattern="INCREMENTAL";
    cfg.getOptionalValue<std::string>(name + ".generatorPattern", cfgGeneratorPattern);
    
    int cfgGeneratorRandomSizeEnabled=0;
    cfg.getOptionalValue<int>(name + ".generatorRandomSizeEnabled", cfgGeneratorRandomSizeEnabled);
    
    std::string cfgLinkMask="0-31";
    cfg.getOptionalValue<std::string>(name + ".linkMask", cfgLinkMask);
    
    std::string cfgReadoutMode="CONTINUOUS";
    cfg.getOptionalValue<std::string>(name + ".readoutMode", cfgReadoutMode);
    
    std::string cfgResetLevel="INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".resetLevel", cfgResetLevel);
        
    // get readout memory buffer parameters
    std::string sMemorySize=cfg.getValue<std::string>(name + ".memoryBufferSize");
    std::string sPageSize=cfg.getValue<std::string>(name + ".memoryPageSize");
    long long mMemorySize=ReadoutUtils::getNumberOfBytesFromString(sMemorySize.c_str());
    long long mPageSize=ReadoutUtils::getNumberOfBytesFromString(sPageSize.c_str());

    // unique identifier based on card ID
    std::string uid="readout." + cardId + "." + std::to_string(cfgChannelNumber);
    //sleep((cfgChannelNumber+1)*2);  // trick to avoid all channels open at once - fail to acquire lock
    
    // create memory pool
    mReadoutMemoryHandler=std::make_shared<ReadoutMemoryHandler>((long)mMemorySize,(int)mPageSize,uid);

    // open and configure ROC
    theLog.log("Opening ROC %s:%d",cardId.c_str(),cfgChannelNumber);
    AliceO2::roc::Parameters params;
    params.setCardId(AliceO2::roc::Parameters::cardIdFromString(cardId));   
    params.setChannelNumber(cfgChannelNumber);

    // setDmaPageSize() : seems deprecated, let's not configure it

    // generator related parameters
    params.setGeneratorEnabled(cfgGeneratorEnabled);
    if (cfgGeneratorEnabled) {
      params.setGeneratorDataSize(cfgGeneratorDataSize);
      params.setGeneratorLoopback(AliceO2::roc::LoopbackMode::fromString(cfgGeneratorLoopback));
      params.setGeneratorPattern(AliceO2::roc::GeneratorPattern::fromString(cfgGeneratorPattern));
      params.setGeneratorRandomSizeEnabled(cfgGeneratorRandomSizeEnabled);
    }
    
    // card readout mode
    params.setReadoutMode(AliceO2::roc::ReadoutMode::fromString(cfgReadoutMode));

    // register the memory block for DMA    
    params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory {
      (void *)mReadoutMemoryHandler->baseAddress, mReadoutMemoryHandler->memorySize
    });
    
    // clear locks if necessary
    params.setForcedUnlockEnabled(true);

    // define link mask
    params.setLinkMask(AliceO2::roc::Parameters::linkMaskFromString(cfgLinkMask));

    // open channel with above parameters
    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);  
    channel->resetChannel(AliceO2::roc::ResetLevel::fromString(cfgResetLevel));

    // todo: log parameters ?

    // start DMA    
    theLog.log("Starting DMA for ROC %s:%d",cardId.c_str(),cfgChannelNumber);
    channel->startDma();    
  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e) << "\n";
    return;
  }
  isInitialized=1;
}



ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {
  if (isInitialized) {
    channel->stopDma();
  }

  theLog.log("Equipment %s : %d pages read",name.c_str(),(int)pageCount);
  theLog.log("Equipment %s : %llu loop count",name.c_str(),loopCount);
}


Thread::CallbackResult  ReadoutEquipmentRORC::populateFifoOut() {
  if (!isInitialized) return  Thread::CallbackResult::Error;
  int isActive=0;

/*
  if (loopCount==0) {
    int s=rand()*10.0/RAND_MAX;
    printf("sleeping %d s\n",s);
    sleep(s);
  }
*/
  
  // get FIFO depth on first iteration (it should be empty)
  if (loopCount==0) {
    RocFifoSize=channel->getTransferQueueAvailable();
    theLog.log("ROC input queue size = %d pages",RocFifoSize);
    if (RocFifoSize==0) {RocFifoSize=1;}
  }
  loopCount++;
    
  // give free pages to the driver
  int nPushed=0;  // number of free pages pushed this iteration 
  while (channel->getTransferQueueAvailable() != 0) {
    long offset=0;
    if (mReadoutMemoryHandler->pagesAvailable->pop(offset)==0) {
      AliceO2::roc::Superpage superpage;
      superpage.offset=offset;
      superpage.size=mReadoutMemoryHandler->pageSize;
      superpage.userData=NULL; // &mReadoutMemoryHandler; // bad - looses shared_ptr
      channel->pushSuperpage(superpage);
      isActive=1;
      nPushed++;
    } else {
      //printf("starving pages\n");
      // todo: log backpressure
      isActive=0;
      break;
    }
  }

  // this is to be called periodically for driver internal business
  channel->fillSuperpages();
  
  // check for completed pages
  int nRead=0; // number of pages read in this iteration
  while ((channel->getReadyQueueSize()>0)) {
    if (dataOut->isFull()) {
      //theLog.log("FIFOout full");
      isActive=0;
      break;
    }
    auto superpage = channel->getSuperpage(); // this is the first superpage in FIFO ... let's check its state
    if (superpage.isFilled()) {
      std::shared_ptr<DataBlockContainerFromRORC>d=nullptr;
      try {
        d=std::make_shared<DataBlockContainerFromRORC>(channel, superpage, mReadoutMemoryHandler);
      }
      catch (...) {
        theLog.log("make_shared<DataBlock> failed");
        break;
      }
      channel->popSuperpage();
      dataOut->push(d); 
      nRead++;
      pageCount++;
      //printf("read page %ld - %d\n",superpage.getOffset(),d.use_count());
      isActive=1;
    } else {
      break;
    }
  }

  // if both FIFOs are mostly empty we can wait a bit before next iteration 
  if ((nPushed<RocFifoSize/4)&&(nRead<RocFifoSize/4)) { 
    isActive=0;
  }
 
  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentRORC>(cfg,cfgEntryPoint);
}
