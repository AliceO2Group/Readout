#include "ReadoutEquipment.h"


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



std::unique_ptr<ReadoutEquipment> getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentDummy>(cfg,cfgEntryPoint);
}
