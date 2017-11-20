#include "ReadoutEquipment.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentDummy : public ReadoutEquipment {

  public:
    ReadoutEquipmentDummy(ConfigFile &cfg, std::string name="dummyReadout");
    ~ReadoutEquipmentDummy();
    DataBlockContainerReference getNextBlock();
    
  private:
    std::shared_ptr<MemPool> mp;  // a memory pool from which to allocate data pages
    Thread::CallbackResult  populateFifoOut(); // iterative callback

    DataBlockId currentId;  // current block id
    int eventMaxSize; // maximum data block size
    int eventMinSize; // minimum data block size
};

ReadoutEquipmentDummy::ReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint) {

  int memPoolNumberOfElements=10000;
  int memPoolElementSize=0.01*1024*1024;

  // get configuration values
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolNumberOfElements", memPoolNumberOfElements);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memPoolElementSize", memPoolElementSize);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMaxSize", eventMaxSize, (int)1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMinSize", eventMinSize, (int)1024);

  // ensure generated events will fit in blocks allocated from memory pool
  int maxElementSize=eventMaxSize+sizeof(DataBlockHeaderBase);
  if (maxElementSize>memPoolElementSize) {
    theLog.log("memPoolElementSize too small, need at least %d bytes",maxElementSize);
    throw __LINE__;
  }

  // create memory pool
  mp=std::make_shared<MemPool>(memPoolNumberOfElements,memPoolElementSize);

  // init variables
  currentId=0;  
}

ReadoutEquipmentDummy::~ReadoutEquipmentDummy() {
  // check if mempool still referenced
  if (!mp.unique()) {
    printf("Warning: mempool still %d references\n",(int)mp.use_count());
  }
} 

DataBlockContainerReference ReadoutEquipmentDummy::getNextBlock() {

  // query memory pool for a free block
  DataBlockContainerReference nextBlock=nullptr;
  try {
    nextBlock=std::make_shared<DataBlockContainerFromMemPool>(mp);
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
    for (int k=0;k<100;k++) {
      b->data[k]=(char)k;
    }
  }  
  return nextBlock;
}



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentDummy>(cfg,cfgEntryPoint);
}
