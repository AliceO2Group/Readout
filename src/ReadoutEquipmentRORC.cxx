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

  
// a big block of memory for I/O
class ReadoutMemoryHandler {
  public:
  size_t memorySize;  // total size of buffer
  int pageSize;       // size of each superpage in buffer (not the one of getpagesize())
  uint8_t * baseAddress; // base address of buffer

  std::unique_ptr<AliceO2::Common::Fifo<long>> pagesAvailable;  // a buffer to keep track of individual pages. storing offset (with respect to base address) of pages available
  
  private:
  std::unique_ptr<AliceO2::roc::MemoryMappedFile> mMemoryMappedFile;
  
  public:
  
  ReadoutMemoryHandler(size_t vMemorySize, int vPageSize, std::string const &uid){
  
    pagesAvailable=nullptr;
    mMemoryMappedFile=nullptr;
    
    std::string memoryMapFilePath="/var/lib/hugetlbfs/global/pagesize-2MB/" + uid;
    //std::string memoryMapFilePath="/var/lib/hugetlbfs/global/pagesize-2MB/test";
      
    // must be multiple of hugepage size
    const int hugePageSize=2*1024*1024;
    int r=vMemorySize % hugePageSize;
    if (r) {
      vMemorySize+=hugePageSize-r;
    }

    theLog.log("Creating shared memory block %ld bytes = %.1f MB @ %s",vMemorySize,(float)(vMemorySize/(1024.0*1024)),memoryMapFilePath.c_str());
    
    try {
      //mMemoryMappedFile=std::make_unique<AliceO2::roc::MemoryMappedFile>(memoryMapFilePath,100*(long)1024*1024,false);
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
    int nPages=memorySize/pageSize;
    theLog.log("Got %d pages, each %d bytes",nPages,pageSize);       
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
    Thread::CallbackResult  populateFifoOut();
    DataBlockId currentId;
    AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel;
    std::shared_ptr<ReadoutMemoryHandler> mReadoutMemoryHandler;
    
    
    int pageCount=0;
    int isInitialized=0;

    unsigned long long loopCount;
};



ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name) : ReadoutEquipment(cfg, name) {

  loopCount=0;
    
  try {

    std::string serialNumber=cfg.getValue<std::string>(name + ".serial");
    int channelNumber=cfg.getValue<int>(name + ".channel");
  
    AliceO2::roc::Parameters::CardIdType cardId;
    if (serialNumber.find(':')!=std::string::npos) {
    	// this looks like a PCI address...
	cardId=AliceO2::roc::PciAddress(serialNumber);
    } else {
    	cardId=std::stoi(serialNumber);
    }
  
    long mMemorySize=cfg.getValue<long>(name + ".memoryBufferSize"); // todo: convert MB to bytes
    int mPageSize=cfg.getValue<int>(name + ".memoryPageSize"); 

    std::string uid="readout." + std::to_string(std::stoi(serialNumber)) + "." + std::to_string(channelNumber);
    //sleep((channelNumber+1)*2);  // trick to avoid all channels open at once - fail to acquire lock
    
    mReadoutMemoryHandler=std::make_shared<ReadoutMemoryHandler>(mMemorySize,mPageSize,uid);

    theLog.log("Opening RORC %s:%d",serialNumber.c_str(),channelNumber);    
    AliceO2::roc::Parameters params;
    params.setCardId(cardId);
    params.setChannelNumber(channelNumber);
    params.setGeneratorPattern(AliceO2::roc::GeneratorPattern::Incremental);
    params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory {
      (void *)mReadoutMemoryHandler->baseAddress, mReadoutMemoryHandler->memorySize
    }); // this registers the memory block for DMA

    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);  
    channel->resetChannel(AliceO2::roc::ResetLevel::Internal);
    channel->startDma();
        
    AliceO2::roc::ChannelFactory::BarSharedPtr bar=AliceO2::roc::ChannelFactory().getBar(params);
    // set random size: address byte 0x420, bit 16
    int wordIndex=0x420/4;
    uint32_t regValue=bar->readRegister(wordIndex);
    //printf("bar read %X\n",regValue);
    regValue|=0x10000;
    //printf("set random size bit: bar write %X\n",regValue);
    bar->writeRegister(wordIndex,regValue);
       
    regValue=bar->readRegister(wordIndex);
    //printf("bar read %X\n",regValue);
    
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
  loopCount++;
    
  // this is to be called periodically for driver internal business
  channel->fillSuperpages();
  
  // give free pages to the driver
  while (channel->getTransferQueueAvailable() != 0) {   
    long offset=0;
    if (mReadoutMemoryHandler->pagesAvailable->pop(offset)==0) {
      AliceO2::roc::Superpage superpage;
      superpage.offset=offset;
      superpage.size=mReadoutMemoryHandler->pageSize;
      superpage.userData=NULL; // &mReadoutMemoryHandler; // bad - looses shared_ptr
      channel->pushSuperpage(superpage);
      isActive=1;
    } else {
//      printf("starving pages\n");
      break;
    }
  }
    
  // check for completed pages
  while ((!dataOut->isFull()) && (channel->getReadyQueueSize()>0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in FIFO ... let's check its state
    if (superpage.isFilled()) {
      std::shared_ptr<DataBlockContainerFromRORC>d=nullptr;
      try {
        d=std::make_shared<DataBlockContainerFromRORC>(channel, superpage, mReadoutMemoryHandler);
      }
      catch (...) {
        break;
      }
      channel->popSuperpage();
      dataOut->push(d); 
      //d=nullptr;
      
      pageCount++;
      //printf("read page %ld - %d\n",superpage.getOffset(),d.use_count());
      isActive=1;
    } else {
      break;
    }
  }
  //return Thread::CallbackResult::Idle;
  //return Thread::CallbackResult::Ok;
  
  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentRORC>(cfg,cfgEntryPoint);
}
