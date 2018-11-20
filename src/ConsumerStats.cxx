#include "Consumer.h"

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>

#include <math.h>
#include <Common/Timer.h>

#include <sys/time.h>
#include <sys/resource.h>



#include <Monitoring/MonitoringFactory.h>
using namespace o2::monitoring;


// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x)/sizeof(x[0])

// function to convert a value in bytes to a prefixed number 3+3 digits
// suffix is the "base unit" to add after calculated prefix, e.g. Byte-> kBytes
std::string NumberOfBytesToString(double value, const char*suffix, int base=1024) {
  const char *prefixes[]={"","k","M","G","T","P"};
  int maxPrefixIndex=STATIC_ARRAY_ELEMENT_COUNT(prefixes)-1;
  int prefixIndex=log(value)/log(base);
  if (prefixIndex>maxPrefixIndex) {
    prefixIndex=maxPrefixIndex;
  }
  if (prefixIndex<0) {
    prefixIndex=0;
  }
  double scaledValue=value/pow(base,prefixIndex);
  char bufStr[64];
  if (suffix==nullptr) {
    suffix="";
  }
  snprintf(bufStr,sizeof(bufStr)-1,"%.03lf %s%s",scaledValue,prefixes[prefixIndex],suffix);
  return std::string(bufStr);
}




class ConsumerStats: public Consumer {
  private:
  uint64_t counterBlocks;
  uint64_t counterBytesTotal;
  uint64_t counterBytesHeader;
  uint64_t counterBytesDiff;
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer monitoringUpdateTimer;
  double elapsedTime; // value used for rates computation
  
  int monitoringEnabled;
  int monitoringUpdatePeriod;
  std::unique_ptr<Monitoring> monitoringCollector;

  struct rusage previousUsage; // variable to keep track of last getrusage() result
  struct rusage currentUsage; // variable to keep track of last getrusage() result  
  double timePreviousGetrusage=0; // variable storing 'runningTime' value when getrusage was previously called (0 if not called yet)
  double cpuUsedOverLastInterval=0; // average CPU usage over latest measurement interval
  
  
  void sendMetricNoException(Metric&& metric, DerivedMetricMode mode = DerivedMetricMode::NONE){
    try {
      monitoringCollector->send(std::forward<Metric &&>(metric),mode);
    }
    catch (...) {
      theLog.log("monitoringCollector->send(%s) failed",metric.getName().c_str());
    }
  }
  
  void publishStats() {
  
    double now=runningTime.getTime();
    getrusage(RUSAGE_SELF,&currentUsage);
    if (timePreviousGetrusage!=0) {
      double tDiff=(now-timePreviousGetrusage)*1000000.0; // delta time in microseconds
      double fractionCpuUsed=
      (
          currentUsage.ru_utime.tv_sec*1000000.0+currentUsage.ru_utime.tv_usec-(previousUsage.ru_utime.tv_sec*1000000.0+previousUsage.ru_utime.tv_usec)
        + currentUsage.ru_stime.tv_sec*1000000.0+currentUsage.ru_stime.tv_usec-(previousUsage.ru_stime.tv_sec*1000000.0+previousUsage.ru_stime.tv_usec)
       ) / tDiff;
      if (monitoringEnabled) {
        sendMetricNoException({fractionCpuUsed*100, "readout.percentCpuUsed"});
        //theLog.log("CPU used = %.2f %%",100*fractionCpuUsed);
      }      
    }
    timePreviousGetrusage=now;
    previousUsage=currentUsage;
    // todo: per thread? -> add feature in Thread class
  
    if (monitoringEnabled) {
      // todo: support for long long types
      // https://alice.its.cern.ch/jira/browse/FLPPROT-69

      sendMetricNoException({counterBlocks, "readout.Blocks"});
//      sendMetricNoException({counterBytesTotal, "readout.BytesTotal"});
      sendMetricNoException({counterBytesTotal, "readout.BytesTotal"}, DerivedMetricMode::RATE);
      sendMetricNoException({counterBytesDiff, "readout.BytesInterval"});
//      sendMetricNoException({(counterBytesTotal/(1024*1024)), "readout.MegaBytesTotal"});

      counterBytesDiff=0;
    }
  }


  public:
  ConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {

    // configuration parameter: | consumer-stats-* | monitoringEnabled | int | 0 | Enable (1) or disable (0) readout monitoring. |
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringEnabled", monitoringEnabled, 0);
    if (monitoringEnabled) {
      // configuration parameter: | consumer-stats-* | monitoringUpdatePeriod | int | 10 | Period of readout monitoring updates. |
      cfg.getOptionalValue(cfgEntryPoint + ".monitoringUpdatePeriod", monitoringUpdatePeriod, 10);
      // configuration parameter: | consumer-stats-* | monitoringURI | string |  | URI to connect O2 monitoring service. c.f. o2::monitoring. |
      const std::string configURI=cfg.getValue<std::string>(cfgEntryPoint + ".monitoringURI");
      theLog.log("Monitoring enabled - period %ds - using %s",monitoringUpdatePeriod,configURI.c_str());

      monitoringCollector=MonitoringFactory::Get(configURI.c_str());

      // enable process monitoring
      // configuration parameter: | consumer-stats-* | processMonitoringInterval | int | 0 | Period of process monitoring updates (O2 standard metrics). If zero (default), disabled.|
      int processMonitoringInterval=0;
      cfg.getOptionalValue(cfgEntryPoint + ".processMonitoringInterval", processMonitoringInterval, 0);
      if (processMonitoringInterval>0) {
        monitoringCollector->enableProcessMonitoring(processMonitoringInterval);
      }

      monitoringUpdateTimer.reset(monitoringUpdatePeriod*1000000);
    }

    counterBytesTotal=0;
    counterBytesHeader=0;
    counterBlocks=0;
    counterBytesDiff=0;
    runningTime.reset();
    elapsedTime=0.0;

  }
  ~ConsumerStats() {
    if (elapsedTime==0) {
      theLog.log("Stopping stats clock");
      elapsedTime=runningTime.getTime();
    }
    if (counterBytesTotal>0) {
      theLog.log("Stats: %llu blocks, %.2f MB, %.2f%% header overhead",(unsigned long long)counterBlocks,counterBytesTotal/(1024*1024.0),counterBytesHeader*100.0/counterBytesTotal);
      theLog.log("Stats: average block size = %llu bytes",(unsigned long long)counterBytesTotal/counterBlocks);
      theLog.log("Stats: average block rate = %s",NumberOfBytesToString((counterBlocks)/elapsedTime,"Hz",1000).c_str());
      theLog.log("Stats: average throughput = %s",NumberOfBytesToString(counterBytesTotal/elapsedTime,"B/s").c_str());
      theLog.log("Stats: elapsed time = %.5lfs",elapsedTime);
      publishStats();
    } else {
      theLog.log("Stats: no data received");
    }
  }
  int pushData(DataBlockContainerReference &b) {
    
    counterBlocks++;
    int newBytes=b->getData()->header.dataSize;
    counterBytesTotal+=newBytes;
    counterBytesDiff+=newBytes;
    counterBytesHeader+=b->getData()->header.headerSize;

    if (monitoringEnabled) {
      // todo: do not check time every push() if it goes fast...
      if (monitoringUpdateTimer.isTimeout()) {
        publishStats();
        monitoringUpdateTimer.increment();
      }
    }

    return 0;
  }
  
  int starting() {
    theLog.log("Starting stats clock");
    runningTime.reset();
    return 0;
  };

  int stopping() {
    theLog.log("Stopping stats clock");
    elapsedTime=runningTime.getTime();
    return 0;
  };

};




std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerStats>(cfg, cfgEntryPoint);
}
