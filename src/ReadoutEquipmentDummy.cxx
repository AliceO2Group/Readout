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
    Thread::CallbackResult  populateFifoOut(); // iterative callback

    DataBlockId currentId; // current block id
    int eventMaxSize; // maximum data block size
    int eventMinSize; // minimum data block size
    int fillData; // if set, data pages filled with incremental values
};

ReadoutEquipmentDummy::ReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint) {

  // get configuration values
  // configuration parameter: | equipment-dummy-* | eventMaxSize | int | 128k | Maximum size of randomly generated event. |
  // configuration parameter: | equipment-dummy-* | eventMinSize | int | 128k | Minimum size of randomly generated event. |
  // configuration parameter: | equipment-dummy-* | fillData | int | 0 | If non-zero, data payload is filled with a counter. Otherwise (default), no write operation is performed, random data from memory is kept in payload. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMaxSize", eventMaxSize, (int)128*1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".eventMinSize", eventMinSize, (int)128*1024);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".fillData", fillData, (int)0);

  // log config summary
  theLog.log("Equipment %s: eventSize: %d -> %d, fillData=%d", name.c_str(), eventMinSize, eventMaxSize, fillData);
  
  // ensure generated events will fit in blocks allocated from memory pool
  int maxElementSize=eventMaxSize+sizeof(DataBlockHeaderBase);
  if (maxElementSize>memoryPoolPageSize) {
    theLog.log("memoryPoolPageSize too small, need at least %d bytes", maxElementSize);
    throw __LINE__;
  }

  // init variables
  currentId=0;  
}

ReadoutEquipmentDummy::~ReadoutEquipmentDummy() {
} 

DataBlockContainerReference ReadoutEquipmentDummy::getNextBlock() {
  // query memory pool for a free block
  DataBlockContainerReference nextBlock=nullptr;
  try {
    nextBlock=mp->getNewDataBlockContainer();
  }
  catch (...) {
  }
  
  // format data block
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
    // todo: align begin of data ?
    b->data=&(((char *)b)[sizeof(DataBlock)]);

    // optionaly fill data range
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
