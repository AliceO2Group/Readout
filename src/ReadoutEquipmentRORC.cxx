#include "ReadoutEquipment.h"

#include <ReadoutCard/Parameters.h>
#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>
#include <ReadoutCard/Driver.h>

#include <string>
#include <mutex>

#include <Common/Timer.h>

#include "ReadoutUtils.h"
#include "RdhUtils.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;




// a big block of memory for I/O
class ReadoutMemoryHandler {
  public:
  size_t memorySize;  // total size of buffer
  size_t pageSize;       // size of each superpage in buffer (not the one of getpagesize())
  uint8_t * baseAddress; // base address of buffer

  std::unique_ptr<AliceO2::Common::Fifo<long>> pagesAvailable;  // a buffer to keep track of individual pages. storing offset (with respect to base address) of pages available
  
  void *getBaseAddress() {
    return baseAddress;
  }
  size_t getSize() {
    return memorySize;
  }
  size_t getPageSize() {
    return pageSize;
  }
  void freePage(void *p) {
    int64_t offset=((uint8_t *)p)-baseAddress;
    if ((offset<0) || (offset>(int64_t)memorySize)) {
      throw __LINE__;
    }
    pagesAvailable->push(offset);
  }
  void *getPage() {
    long offset=0;
    int res=pagesAvailable->pop(offset);
    if (res==0) {
      uint8_t *pagePtr=&baseAddress[offset];
      return pagePtr;
    }
    return nullptr;
  }
  
  private:
  std::unique_ptr<AliceO2::roc::MemoryMappedFile> mMemoryMappedFile;
  
  public:
  
  ReadoutMemoryHandler(size_t vMemorySize, int vPageSize, std::string const &uid, std::string const &hugePageSize="1GB"){
  
    pagesAvailable=nullptr;
    mMemoryMappedFile=nullptr;
    
    // select Huge Page type and define corresponding settings
    int hugePageSizeBytes; // size of page in bytes    
    if (hugePageSize=="1GB") {
      hugePageSizeBytes=1024*1024*1024;
    } else if (hugePageSize=="2MB") {
      hugePageSizeBytes=2*1024*1024;
    } else {
      theLog.log("Wrong hugePageSize %s",hugePageSize.c_str());
      exit(1);
    }    
    
    // path to our memory segment
    std::string memoryMapBasePath="/var/lib/hugetlbfs/global/pagesize-"+hugePageSize+"/";
    std::string memoryMapFilePath=memoryMapBasePath + uid;
      
    // must be multiple of hugepage size
    int r=vMemorySize % hugePageSizeBytes;
    if (r) {
      vMemorySize+=hugePageSizeBytes-r;
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
//  std::shared_ptr<ReadoutMemoryHandler> mReadoutMemoryHandler;  // todo: store this in superpage user data
  std::shared_ptr<MemoryHandler> mReadoutMemoryHandler;  // a memory pool from which to allocate data pages

  public:
  DataBlockContainerFromRORC(AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel, AliceO2::roc::Superpage  const & superpage, //std::shared_ptr<ReadoutMemoryHandler> const & h) {
  std::shared_ptr<MemoryHandler> const & h) {
  
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
    
    uint32_t *ptr=(uint32_t *) & (((uint8_t *)h->getBaseAddress())[mSuperpage.getOffset()]);
    
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
    //mReadoutMemoryHandler->pagesAvailable->push(mSuperpage.getOffset());

    void *ptr= & (((uint8_t *)mReadoutMemoryHandler->getBaseAddress())[mSuperpage.getOffset()]);
    mReadoutMemoryHandler->freePage(ptr);
    
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

    Thread::CallbackResult prepareBlocks();
    DataBlockContainerReference getNextBlock(); 
  
  private:
    Thread::CallbackResult  populateFifoOut(); // the data readout loop function
    
    AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel;    // channel to ROC device
    //std::shared_ptr<ReadoutMemoryHandler> mReadoutMemoryHandler;  // object to get memory from
    std::shared_ptr<MemoryHandler> mReadoutMemoryHandler;  // object to get memory from

    DataBlockId currentId=0;    // current data id, kept for auto-increment
       
    bool isInitialized=false;     // flag set to 1 when class has been successfully initialized
    bool isWaitingFirstLoop=true;  // flag set until first readout loop called

    int RocFifoSize=0;  // detected size of ROC fifo (when filling it for the first time)

    int cfgRdhCheckEnabled=0; // flag to enable RDH check at runtime
    int cfgRdhDumpEnabled=0;  // flag to enable RDH dump at runtime

    unsigned long long statsRdhCheckOk=0;   // number of RDH structs which have passed check ok
    unsigned long long statsRdhCheckErr=0;  // number of RDH structs which have not passed check    
    
    AliceO2::Common::Timer timeframeClock;	// timeframe id should be increased at each clock cycle
    int currentTimeframe=0;	// id of current timeframe
};


std::mutex readoutEquipmentRORCLock;
bool isDriverInitialized=false;


ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name) : ReadoutEquipment(cfg, name) {
   
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
    
    std::string cfgGeneratorLoopback="INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".generatorLoopback", cfgGeneratorLoopback);

    std::string cfgGeneratorPattern="INCREMENTAL";
    cfg.getOptionalValue<std::string>(name + ".generatorPattern", cfgGeneratorPattern);
    
    int cfgGeneratorRandomSizeEnabled=0;
    cfg.getOptionalValue<int>(name + ".generatorRandomSizeEnabled", cfgGeneratorRandomSizeEnabled);
    
    std::string cfgLinkMask="0-31";
    cfg.getOptionalValue<std::string>(name + ".linkMask", cfgLinkMask);
    
    //std::string cfgReadoutMode="CONTINUOUS";
    //cfg.getOptionalValue<std::string>(name + ".readoutMode", cfgReadoutMode);
    
    std::string cfgResetLevel="INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".resetLevel", cfgResetLevel);

    // extra configuration parameters    
    cfg.getOptionalValue<int>(name + ".rdhCheckEnabled", cfgRdhCheckEnabled);
    cfg.getOptionalValue<int>(name + ".rdhDumpEnabled", cfgRdhDumpEnabled);
        
    // get readout memory buffer parameters
    std::string sMemorySize=cfg.getValue<std::string>(name + ".memoryBufferSize");
    std::string sPageSize=cfg.getValue<std::string>(name + ".memoryPageSize");
    long long mMemorySize=ReadoutUtils::getNumberOfBytesFromString(sMemorySize.c_str());
    long long mPageSize=ReadoutUtils::getNumberOfBytesFromString(sPageSize.c_str());

    std::string cfgHugePageSize="1GB";
    cfg.getOptionalValue<std::string>(name + ".memoryHugePageSize",cfgHugePageSize);

    // unique identifier based on card ID
    std::string uid="readout." + cardId + "." + std::to_string(cfgChannelNumber);
    //sleep((cfgChannelNumber+1)*2);  // trick to avoid all channels open at once - fail to acquire lock
    
    // make sure ROC driver is initialized once
    readoutEquipmentRORCLock.lock();       
    if (!isDriverInitialized) {
      AliceO2::roc::driver::initialize();
      isDriverInitialized=true;
    }
    readoutEquipmentRORCLock.unlock();
    
    // create memory pool
    //mReadoutMemoryHandler=std::make_shared<ReadoutMemoryHandler>((long)mMemorySize,(int)mPageSize,uid,cfgHugePageSize);
    mReadoutMemoryHandler=std::make_shared<MemoryHandler>(mPageSize,mMemorySize/mPageSize);

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

    // card readout mode : experimental, not needed
    // params.setReadoutMode(AliceO2::roc::ReadoutMode::fromString(cfgReadoutMode));    

    // register the memory block for DMA
  
    
    theLog.log("Loop DMA block %p:%lu",(void *)mReadoutMemoryHandler->getBaseAddress(),mReadoutMemoryHandler->getSize());
    char *ptr=(char *)mReadoutMemoryHandler->getBaseAddress();
    for (size_t i=0;i<mReadoutMemoryHandler->getSize();i++) {
      ptr[i]=0;
    }
    theLog.log("Register DMA block %p:%lu",(void *)mReadoutMemoryHandler->getBaseAddress(),mReadoutMemoryHandler->getSize());
    params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory {
//      (void *)mReadoutMemoryHandler->baseAddress, mReadoutMemoryHandler->memorySize
      (void *)mReadoutMemoryHandler->getBaseAddress(), mReadoutMemoryHandler->getSize()
    });
    
    // clear locks if necessary
    params.setForcedUnlockEnabled(true);

    // define link mask
    // this is harmless for C-RORC
    params.setLinkMask(AliceO2::roc::Parameters::linkMaskFromString(cfgLinkMask));

    // open channel with above parameters
    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);  
    channel->resetChannel(AliceO2::roc::ResetLevel::fromString(cfgResetLevel));

    // retrieve card information
    std::string infoPciAddress=channel->getPciAddress().toString();
    int infoNumaNode=channel->getNumaNode();
    std::string infoSerialNumber="unknown";
    auto v_infoSerialNumber=channel->getSerial();
    if (v_infoSerialNumber) {
      infoSerialNumber=std::to_string(v_infoSerialNumber.get());
    }
    std::string infoFirmwareVersion=channel->getFirmwareInfo().value_or("unknown");
    std::string infoCardId=channel->getCardId().value_or("unknown");
    theLog.log("Equipment %s : PCI %s @ NUMA node %d, serial number %s, firmware version %s, card id %s", name.c_str(), infoPciAddress.c_str(), infoNumaNode, infoSerialNumber.c_str(),
    infoFirmwareVersion.c_str(), infoCardId.c_str());
    

    // todo: log parameters ?

    // start DMA    
    theLog.log("Starting DMA for ROC %s:%d",cardId.c_str(),cfgChannelNumber);
    channel->startDma();    
    
    // get FIFO depth (it should be fully empty when starting)
    RocFifoSize=channel->getTransferQueueAvailable();
    theLog.log("ROC input queue size = %d pages",RocFifoSize);
    if (RocFifoSize==0) {RocFifoSize=1;}

    // reset timeframe clock
    timeframeClock.reset(1000000/50.0); // 50Hz rate
    currentTimeframe=0;

  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e) << "\n";
    return;
  }
  isInitialized=true;
}



ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {
  if (isInitialized) {
    channel->stopDma();
  }

  if (cfgRdhCheckEnabled) {
    theLog.log("Equipment %s : RDH checks %llu ok, %llu errors",name.c_str(),statsRdhCheckOk,statsRdhCheckErr);  
  }
}


Thread::CallbackResult ReadoutEquipmentRORC::prepareBlocks(){
  if (!isInitialized) return  Thread::CallbackResult::Error;
  int isActive=0;
  
  // keep track of situations where the queue is completely empty
  // this means we have not filled it fast enough (except in first loop, where it's normal it is empty)
  if (isWaitingFirstLoop) {
    isWaitingFirstLoop=false;
  } else {
    int nFreeSlots=channel->getTransferQueueAvailable();
    if (nFreeSlots == RocFifoSize) {  
      equipmentStats[EquipmentStatsIndexes::nFifoUpEmpty].increment();
    }
    equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].set(nFreeSlots);
  }
  
  // give free pages to the driver
  int nPushed=0;  // number of free pages pushed this iteration 
  while (channel->getTransferQueueAvailable() != 0) {
    long offset=0;
    void *newPage=mReadoutMemoryHandler->getPage();
    //if (mReadoutMemoryHandler->pagesAvailable->pop(offset)==0) {
    if (newPage!=nullptr) {
      AliceO2::roc::Superpage superpage;
      //superpage.offset=offset;
      superpage.offset=(char *)newPage-(char *)mReadoutMemoryHandler->getBaseAddress();
      superpage.size=mReadoutMemoryHandler->getPageSize();
      superpage.userData=NULL; // &mReadoutMemoryHandler; // bad - looses shared_ptr
      channel->pushSuperpage(superpage);
      isActive=1;
      nPushed++;
    } else {
      equipmentStats[EquipmentStatsIndexes::nMemoryLow].increment();
      isActive=0;
      break;
    }
  }
  equipmentStats[EquipmentStatsIndexes::nPushedUp].increment(nPushed);

  // check fifo occupancy ready queue size for stats
  equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].set(channel->getReadyQueueSize());
  if (channel->getReadyQueueSize()==RocFifoSize) {
    equipmentStats[EquipmentStatsIndexes::nFifoReadyFull].increment();  
  }

  // if we have not put many pages (<25%) in ROC fifo, we can wait a bit
  if (nPushed<RocFifoSize/4) { 
    isActive=0;
  }


  // This global mutex was also used as a fix to allow reading out 2 CRORC at same time
  // otherwise machine reboots when ACPI is not OFF
  //readoutEquipmentRORCLock.lock();
  
  // this is to be called periodically for driver internal business
  channel->fillSuperpages();
  
  //readoutEquipmentRORCLock.unlock();


  // from time to time, we may monitor temperature
//      virtual boost::optional<float> getTemperature() = 0;


  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}


DataBlockContainerReference ReadoutEquipmentRORC::getNextBlock() {

  DataBlockContainerReference nextBlock=nullptr;
  
  //channel->fillSuperpages();
    
  // check for completed page
  if ((channel->getReadyQueueSize()>0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in FIFO ... let's check its state
    if (superpage.isFilled()) {
      std::shared_ptr<DataBlockContainerFromRORC>d=nullptr;
      try {
        d=std::make_shared<DataBlockContainerFromRORC>(channel, superpage, mReadoutMemoryHandler);
      }
      catch (...) {
        // todo: increment a stats counter?
        theLog.log("make_shared<DataBlock> failed");
      }
      if (d!=nullptr) {
        channel->popSuperpage();
        nextBlock=d;

	if (timeframeClock.isTimeout()) {
	  currentTimeframe++;
	  timeframeClock.increment();
	}
	d->getData()->header.id=currentTimeframe;
        
        // validate RDH structure, if configured to do so
        if (cfgRdhCheckEnabled) {
          std::string errorDescription;
          size_t blockSize=d->getData()->header.dataSize;
          uint8_t *baseAddress=(uint8_t *)(d->getData()->data);
          for (size_t pageOffset=0;pageOffset<blockSize;) {
            RdhHandle h(baseAddress+pageOffset);
            if (h.validateRdh(errorDescription)) {
              if (cfgRdhDumpEnabled) {             
                printf("Page 0x%p + %ld\n%s",(void *)baseAddress,pageOffset,errorDescription.c_str());
                h.dumpRdh();
                errorDescription.clear();
              }
              statsRdhCheckErr++;
            } else {
              statsRdhCheckOk++;
            }
            pageOffset+=h.getBlockLengthBytes();
          }
        }
      }
    }
  }
  return nextBlock;
}


std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentRORC>(cfg,cfgEntryPoint);
}
