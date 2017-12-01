#include "ReadoutEquipment.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentCruEmulator : public ReadoutEquipment {

  public:
    ReadoutEquipmentCruEmulator(ConfigFile &cfg, std::string name="CruEmulatorReadout");
    ~ReadoutEquipmentCruEmulator();
    DataBlockContainerReference getNextBlock();
    
  private:
    std::shared_ptr<MemoryHandler> mh;  // a memory pool from which to allocate data pages
    Thread::CallbackResult  populateFifoOut(); // iterative callback

    DataBlockId currentId;  // current block id
    int eventMaxSize; // maximum data block size
    int eventMinSize; // minimum data block size
};

ReadoutEquipmentCruEmulator::ReadoutEquipmentCruEmulator(ConfigFile &cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint) {

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
  if (bigBlock==nullptr) {
    theLog.log("big block unavailable for output");
    throw __LINE__;
  } else {
    theLog.log("Using big block @ %p",bigBlock->ptr);
    mh=std::make_shared<MemoryHandler>(memPoolElementSize,memPoolNumberOfElements);
  }
  
  // init variables
  currentId=0;  
}

ReadoutEquipmentCruEmulator::~ReadoutEquipmentCruEmulator() {
} 

DataBlockContainerReference ReadoutEquipmentCruEmulator::getNextBlock() {

  // query memory pool for a free block
  DataBlockContainerReference nextBlock=nullptr;
  try {
    //nextBlock=std::make_shared<DataBlockContainerFromMemPool>(mp);
    nextBlock=std::make_shared<DataBlockContainerFromMemoryHandler>(mh);
  }
  catch (...) {
  }
  if (nextBlock!=nullptr) {
    DataBlock *b=nextBlock->getData(); 

    //theLog.log("generating block @ %p",b);

    // set size
    int dSize=(int)(eventMinSize+(int)((eventMaxSize-eventMinSize)*(rand()*1.0/RAND_MAX)));

    // no need to check size fits in page, this was done once for all at configure time

    // fill header
    currentId++;  // don't start from 0
    b->header.blockType=DataBlockType::H_BASE;
    b->header.headerSize=sizeof(DataBlockHeaderBase);
    b->header.dataSize=dSize;
    b->header.id=currentId;
  
    // b->data is set when creating block

    // fill (a bit of) data
    for (int k=0;k<100;k++) {
      b->data[k]=(char)k;
    }
  }  
  return nextBlock;
}



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentCruEmulator(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentCruEmulator>(cfg,cfgEntryPoint);
}
