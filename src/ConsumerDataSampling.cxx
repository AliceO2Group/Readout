#include "Consumer.h"

class ConsumerDataSampling: public Consumer {
  public: 
  ConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
 
  }  
  ~ConsumerDataSampling() {
 
  }
  int pushData(DataBlockContainerReference) {
    return 0;
  }
  private:
};





std::unique_ptr<Consumer> getUniqueConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerDataSampling>(cfg, cfgEntryPoint);
}
