#include "ReadoutEquipment.h"
#include "MemoryBankManager.h"
#include "ReadoutUtils.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;


class ReadoutEquipmentDummy : public ReadoutEquipment {

  public:
    ReadoutEquipmentDummy(ConfigFile &cfg, std::string name="dummyReadout");
    ~ReadoutEquipmentDummy();
    DataBlockContainerReference getNextBlock();
    
  private:
    std::shared_ptr<MemoryPagesPool> mp; // a memory pool from which to allocate data pages
    Thread::CallbackResult  populateFifoOut(); // iterative callback

    DataBlockId currentId; // current block id
    int eventMaxSize; // maximum data block size
    int eventMinSize; // minimum data block size
    int fillData; // if set, data pages filled with incremental values
};

ReadoutEquipmentDummy::ReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint) {

  int memoryPoolPageSize=0.01*1024*1024;
  int memoryPoolNumberOfPages=10000;
  std::string memoryBankName=""; // by default, this uses the first memory bank available
  
  // get configuration values
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryBankName", memoryBankName);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memoryPoolPageSize", memoryPoolPageSize);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memoryPoolNumberOfPages", memoryPoolNumberOfPages);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMaxSize", eventMaxSize, (int)1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMinSize", eventMinSize, (int)1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".fillData", fillData, (int)0);

  theLog.log("%s : buffer %d pages x %d bytes, eventSize: %d -> %d",
  cfgEntryPoint.c_str(),(int) memoryPoolNumberOfPages, (int) memoryPoolPageSize, eventMinSize, eventMaxSize);
  
  
  // ensure generated events will fit in blocks allocated from memory pool
  int maxElementSize=eventMaxSize+sizeof(DataBlockHeaderBase);
  if (maxElementSize>memoryPoolPageSize) {
    theLog.log("memoryPoolPageSize too small, need at least %d bytes",maxElementSize);
    throw __LINE__;
  }

  // create memory pool
  mp=theMemoryBankManager.getPagedPool(memoryPoolPageSize, memoryPoolNumberOfPages, memoryBankName);

  // init variables
  currentId=0;  
}

ReadoutEquipmentDummy::~ReadoutEquipmentDummy() {
  // check if mempool still referenced
  if (!mp.unique()) {
    printf("Warning: mempool still has %d references\n",(int)mp.use_count());
  }
} 

DataBlockContainerReference ReadoutEquipmentDummy::getNextBlock() {

  // query memory pool for a free block
  DataBlockContainerReference nextBlock=nullptr;
  try {
    nextBlock=mp->getNewDataBlockContainer();
  }
  catch (...) {
  }
  if (nextBlock!=nullptr) {
    DataBlock *b=nextBlock->getData(); 

    // set size
    int dSize=(int)(eventMinSize+(int)((eventMaxSize-eventMinSize)*(rand()*1.0/RAND_MAX)));

    // no need to check size fits in page, this was done once for all at configure time

    // fill header
    currentId++;  // don't start from 0
    b->header.blockType=DataBlockType::H_BASE;
    b->header.headerSize=sizeof(DataBlockHeaderBase);
    b->header.dataSize=dSize;
    b->header.id=currentId;
    // say it's contiguous header+data 
    // todo: align begin of data 
    b->data=&(((char *)b)[sizeof(DataBlock)]);

    // fill (a bit of) data
    if (fillData==1) {
      for (int k=0;k<dSize;k++) {
        b->data[k]=(char)k;
      }
    }
  }  
  return nextBlock;
}



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentDummy>(cfg,cfgEntryPoint);
}
