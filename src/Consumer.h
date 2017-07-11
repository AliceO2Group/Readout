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


std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFMQ(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerDataChecker(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<Consumer> getUniqueConsumerDataSampling(ConfigFile &cfg, std::string cfgEntryPoint);


