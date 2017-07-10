#include <Common/Configuration.h>


#include <DataFormat/DataBlock.h>
#include <DataFormat/DataBlockContainer.h>
#include <DataFormat/DataSet.h>

#include <memory>


#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;


class Consumer {
  public:
  Consumer(ConfigFile &, std::string) {
  };
  virtual ~Consumer() {
  };
  virtual int pushData(DataBlockContainerReference b)=0;
  
  protected:
    InfoLogger theLog;
};


std::shared_ptr<Consumer> getSharedConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint);
std::shared_ptr<Consumer> getSharedConsumerFMQ(ConfigFile &cfg, std::string cfgEntryPoint);
std::shared_ptr<Consumer> getSharedConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint);
std::shared_ptr<Consumer> getSharedConsumerDataChecker(ConfigFile &cfg, std::string cfgEntryPoint);
std::shared_ptr<Consumer> getSharedConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint);


