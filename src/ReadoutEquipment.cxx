#include "ReadoutEquipment.h"


ReadoutEquipment::ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint) {
  
  // example: browse config keys
  //for (auto cfgKey : ConfigFileBrowser (&cfg,"",cfgEntryPoint)) {
  //  std::string cfgValue=cfg.getValue<std::string>(cfgEntryPoint + "." + cfgKey);
  //  printf("%s.%s = %s\n",cfgEntryPoint.c_str(),cfgKey.c_str(),cfgValue.c_str());
  //}

  
  // by default, name the equipment as the config node entry point
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".name", name, cfgEntryPoint);

  // target readout rate in Hz, -1 for unlimited (default)
  cfg.getOptionalValue<double>("readout.rate",readoutRate,-1.0);


  readoutThread=std::make_unique<Thread>(ReadoutEquipment::threadCallback,this,name,1000);

  int outFifoSize=1000;
  
  dataOut=std::make_shared<AliceO2::Common::Fifo<DataBlockContainerReference>>(outFifoSize);
  nBlocksOut=0;
}

const std::string & ReadoutEquipment::getName() {
  return name;
}

void ReadoutEquipment::start() {
  readoutThread->start();
  if (readoutRate>0) {
    clk.reset(1000000.0/readoutRate);
  }
  clk0.reset();
}

void ReadoutEquipment::stop() {
  readoutThread->stop();
  //printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocksOut,clk0.getTimer(),nBlocksOut/clk0.getTime());
  readoutThread->join();
}

ReadoutEquipment::~ReadoutEquipment() {
//  printf("deleted %s\n",name.c_str());
}




DataBlockContainerReference ReadoutEquipment::getBlock() {
  DataBlockContainerReference b=nullptr;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult  ReadoutEquipment::threadCallback(void *arg) {
  ReadoutEquipment *ptr=static_cast<ReadoutEquipment *>(arg);
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

