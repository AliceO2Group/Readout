#include "Consumer.h"
#include "ReadoutUtils.h"

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>

#include <math.h>
#include <Common/Timer.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <unordered_map>


#include <Monitoring/MonitoringFactory.h>
using namespace o2::monitoring;


// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x)/sizeof(x[0])



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
  
  // per-equipment statistics
  struct EquipmentStats {
    uint64_t counterBytesPayload=0;
  };
  typedef std::unordered_map<uint16_t, EquipmentStats> EquipmentStatsMap;
  EquipmentStatsMap equipmentStatsMap;
  
  
  
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

      // per-equipment stats
      for (auto &it : equipmentStatsMap) {
        std::string metricName= "readout.BytesEquipment." + std::to_string(it.first);
	//sendMetricNoException(Metric{it.second.counterBytesPayload, "readout.BytesEquipment"}.addTags({(unsigned int)it.first}), DerivedMetricMode::RATE);
	sendMetricNoException(Metric{it.second.counterBytesPayload, metricName}, DerivedMetricMode::RATE);
      }
      
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
      theLog.log("Statistics for %s",this->name.c_str());
      theLog.log("Stats: %llu blocks, %.2f MB, %.2f%% header overhead",(unsigned long long)counterBlocks,counterBytesTotal/(1024*1024.0),counterBytesHeader*100.0/counterBytesTotal);
      theLog.log("Stats: average block size = %llu bytes",(unsigned long long)counterBytesTotal/counterBlocks);
      theLog.log("Stats: average block rate = %s",NumberOfBytesToString((counterBlocks)/elapsedTime,"Hz",1000).c_str());
      theLog.log("Stats: average throughput = %s",NumberOfBytesToString(counterBytesTotal/elapsedTime,"B/s").c_str());
      theLog.log("Stats: average throughput = %s",NumberOfBytesToString(counterBytesTotal*8/elapsedTime,"bits/s",1000).c_str());
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

    // per-equipment stats
    uint16_t eqId=b->getData()->header.equipmentId;
    if (eqId!=undefinedEquipmentId) {
      // is there already a stats counter for this equipment?
      auto it=equipmentStatsMap.find(eqId);
      if (it == equipmentStatsMap.end()) {
        // no matching equipment found, add it to the list
        EquipmentStats newStats;
      	equipmentStatsMap.insert({eqId,newStats});
      } else {
        // equipment found, update counters
        it->second.counterBytesPayload+=b->getData()->header.dataSize;
      }
    }    

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
