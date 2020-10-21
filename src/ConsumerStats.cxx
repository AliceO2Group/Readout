// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "Consumer.h"
#include "ReadoutUtils.h"

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"

#include <memory>

#include <Common/Timer.h>
#include <math.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <unordered_map>

#include <Monitoring/MonitoringFactory.h>
using namespace o2::monitoring;

// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x) / sizeof(x[0])

class ConsumerStats : public Consumer {
private:
  uint64_t counterBlocks;
  uint64_t counterBlocksDiff;
  uint64_t counterBytesTotal;
  uint64_t counterBytesHeader;
  uint64_t counterBytesDiff;
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer monitoringUpdateTimer;
  double elapsedTime;       // value used for rates computation
  double intervalStartTime; // counter for interval statistics, keeps last
                            // timestamp

  int monitoringEnabled;
  double monitoringUpdatePeriod;
  std::unique_ptr<Monitoring> monitoringCollector;
  int consoleUpdate; // if set, stats will be published also on console

  struct rusage
      previousUsage; // variable to keep track of last getrusage() result
  struct rusage
      currentUsage; // variable to keep track of last getrusage() result
  double timePreviousGetrusage;   // variable storing 'runningTime' value when
                                  // getrusage was previously called (0 if not
                                  // called yet)
  double cpuUsedOverLastInterval; // average CPU usage over latest measurement
                                  // interval

  // per-equipment statistics
  struct EquipmentStats {
    uint64_t counterBytesPayload = 0;
  };
  typedef std::unordered_map<uint16_t, EquipmentStats> EquipmentStatsMap;
  EquipmentStatsMap equipmentStatsMap;

  bool isRunning = false;

  // reset all counters and timers for a fresh start
  // must be called once before first publishStats() call
  void reset() {
    counterBlocks = 0;
    counterBlocksDiff = 0;
    counterBytesTotal = 0;
    counterBytesHeader = 0;
    counterBytesDiff = 0;
    elapsedTime = 0.0;
    intervalStartTime = 0;
    timePreviousGetrusage = 0;
    cpuUsedOverLastInterval = 0.0;
    equipmentStatsMap.clear();
    monitoringUpdateTimer.reset(monitoringUpdatePeriod * 1000000);
    runningTime.reset();
  }

  void sendMetricNoException(Metric &&metric,
                             DerivedMetricMode mode = DerivedMetricMode::NONE) {
    try {
      monitoringCollector->send(std::forward<Metric &&>(metric), mode);
    } catch (...) {
      theLog.log(LogErrorDevel_(3234), "monitoringCollector->send(%s) failed",
                 metric.getName().c_str());
    }
  }

  void publishStats() {

    double now = runningTime.getTime();
    getrusage(RUSAGE_SELF, &currentUsage);
    if (timePreviousGetrusage != 0) {
      double tDiff = (now - timePreviousGetrusage) *
                     1000000.0; // delta time in microseconds
      double fractionCpuUsed = (currentUsage.ru_utime.tv_sec * 1000000.0 +
                                currentUsage.ru_utime.tv_usec -
                                (previousUsage.ru_utime.tv_sec * 1000000.0 +
                                 previousUsage.ru_utime.tv_usec) +
                                currentUsage.ru_stime.tv_sec * 1000000.0 +
                                currentUsage.ru_stime.tv_usec -
                                (previousUsage.ru_stime.tv_sec * 1000000.0 +
                                 previousUsage.ru_stime.tv_usec)) /
                               tDiff;
      if (monitoringEnabled) {
        sendMetricNoException(
            {fractionCpuUsed * 100, "readout.percentCpuUsed"});
        // theLog.log(LogDebugTrace,"CPU used = %.2f %%",100*fractionCpuUsed);
      }
    }
    timePreviousGetrusage = now;
    previousUsage = currentUsage;
    // todo: per thread? -> add feature in Thread class

    if (consoleUpdate) {
      if (intervalStartTime) {
        double intervalTime = now - intervalStartTime;
        if (intervalTime > 0) {
          theLog.log(LogInfoOps_(3003), "Last interval (%.2fs): blocksRx=%llu, block rate=%.2lf, bytesRx=%llu, rate=%s",
                     intervalTime, (unsigned long long)counterBlocksDiff,
                     counterBlocksDiff / intervalTime,
                     (unsigned long long)counterBytesDiff,
                     NumberOfBytesToString(counterBytesDiff * 8 / intervalTime,
                                           "b/s", 1000)
                         .c_str());
        }
      }
      intervalStartTime = now;
    }

    if (monitoringEnabled) {
      // todo: support for long long types
      // https://alice.its.cern.ch/jira/browse/FLPPROT-69

      sendMetricNoException({counterBlocks, "readout.Blocks"});
      //      sendMetricNoException({counterBytesTotal, "readout.BytesTotal"});
      sendMetricNoException({counterBytesTotal, "readout.BytesTotal"},
                            DerivedMetricMode::RATE);
      sendMetricNoException({counterBytesDiff, "readout.BytesInterval"});
      //      sendMetricNoException({(counterBytesTotal/(1024*1024)),
      //      "readout.MegaBytesTotal"});

      // per-equipment stats
      for (auto &it : equipmentStatsMap) {
        std::string metricName =
            "readout.BytesEquipment." + std::to_string(it.first);
        // sendMetricNoException(Metric{it.second.counterBytesPayload,
        // "readout.BytesEquipment"}.addTags({(unsigned int)it.first}),
        // DerivedMetricMode::RATE);
        sendMetricNoException(Metric{it.second.counterBytesPayload, metricName},
                              DerivedMetricMode::RATE);
      }
    }

    counterBytesDiff = 0;
    counterBlocksDiff = 0;
  }

  // run a function in a separate thread to publish data regularly, until flag
  // isShutdown is set
  std::unique_ptr<std::thread>
      periodicUpdateThread;          // the thread running periodic updates
  bool periodicUpdateThreadShutdown; // flag to stop periodicUpdateThread
  void periodicUpdate() {
    periodicUpdateThreadShutdown = 0;

    // periodic update
    for (; !periodicUpdateThreadShutdown;) {
      if (!isRunning) {
        usleep(100000);
        continue;
      }

      double timeUntilTimeout =
          monitoringUpdateTimer.getRemainingTime(); // measured in seconds
      if (timeUntilTimeout <= 0) {
        publishStats();
        monitoringUpdateTimer.increment();
      } else {
        if (timeUntilTimeout > 1) {
          timeUntilTimeout =
              1.0; // avoid a sleep longer than 1s to allow exiting promptly
        }
        usleep((int)(timeUntilTimeout * 1000000));
      }
    }
  }

public:
  ConsumerStats(ConfigFile &cfg, std::string cfgEntryPoint)
      : Consumer(cfg, cfgEntryPoint) {

    // configuration parameter: | consumer-stats-* | monitoringEnabled | int | 0
    // | Enable (1) or disable (0) readout monitoring. |
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringEnabled",
                         monitoringEnabled, 0);
    // configuration parameter: | consumer-stats-* | monitoringUpdatePeriod |
    // double | 10 | Period of readout monitoring updates, in seconds. |
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringUpdatePeriod",
                         monitoringUpdatePeriod, 10.0);

    if (monitoringEnabled) {
      // configuration parameter: | consumer-stats-* | monitoringURI | string |
      // | URI to connect O2 monitoring service. c.f. o2::monitoring. |
      const std::string configURI =
          cfg.getValue<std::string>(cfgEntryPoint + ".monitoringURI");

      theLog.log(LogInfoDevel_(3002), "Monitoring enabled - period %.2fs - using %s",
                 monitoringUpdatePeriod, configURI.c_str());
      monitoringCollector = MonitoringFactory::Get(configURI.c_str());
      monitoringCollector->addGlobalTag(tags::Key::Subsystem, tags::Value::Readout);

      // enable process monitoring
      // configuration parameter: | consumer-stats-* | processMonitoringInterval
      // | int | 0 | Period of process monitoring updates (O2 standard metrics).
      // If zero (default), disabled.|
      int processMonitoringInterval = 0;
      cfg.getOptionalValue(cfgEntryPoint + ".processMonitoringInterval",
                           processMonitoringInterval, 0);
      if (processMonitoringInterval > 0) {
        monitoringCollector->enableProcessMonitoring(processMonitoringInterval);
      }
    }

    // configuration parameter: | consumer-stats-* | consoleUpdate | int | 0 |
    // If non-zero, periodic updates also output on the log console (at rate
    // defined in monitoringUpdatePeriod). If zero, periodic log output is
    // disabled. |
    cfg.getOptionalValue(cfgEntryPoint + ".consoleUpdate", consoleUpdate, 0);
    if (consoleUpdate) {
      theLog.log(LogInfoDevel_(3002), "Periodic console statistics enabled");
    }

    // make sure to initialize all counters and timers
    reset();

    // start thread for periodic updates
    std::function<void(void)> l =
        std::bind(&ConsumerStats::periodicUpdate, this);
    periodicUpdateThread = std::make_unique<std::thread>(l);
  }

  ~ConsumerStats() {
    if (isRunning) {
      stop();
    }

    periodicUpdateThreadShutdown = 1;
    periodicUpdateThread->join();
  }

  int pushData(DataBlockContainerReference &b) {

    counterBlocks++;
    counterBlocksDiff++;
    int newBytes = b->getData()->header.dataSize;
    counterBytesTotal += newBytes;
    counterBytesDiff += newBytes;
    counterBytesHeader += b->getData()->header.headerSize;

    // per-equipment stats
    uint16_t eqId = b->getData()->header.equipmentId;
    if (eqId != undefinedEquipmentId) {
      // is there already a stats counter for this equipment?
      auto it = equipmentStatsMap.find(eqId);
      if (it == equipmentStatsMap.end()) {
        // no matching equipment found, add it to the list
        EquipmentStats newStats;
        equipmentStatsMap.insert({eqId, newStats});
      } else {
        // equipment found, update counters
        it->second.counterBytesPayload += b->getData()->header.dataSize;
      }
    }

    return 0;
  }

  int start() {
    Consumer::start();
    theLog.log(LogInfoDevel_(3006), "Starting stats clock");
    reset();

    // publish once on start
    publishStats();

    isRunning = true;
    return 0;
  };

  int stop() {
    isRunning = false;
    theLog.log(LogInfoDevel_(3006), "Stopping stats clock");
    elapsedTime = runningTime.getTime();

    // publish once more on stop
    publishStats();

    if (counterBytesTotal > 0) {
      theLog.log(LogInfoDevel_(3003), "Statistics for %s", this->name.c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: %llu blocks, %.2f MB, %.2f%% header overhead",
                 (unsigned long long)counterBlocks,
                 counterBytesTotal / (1024 * 1024.0),
                 counterBytesHeader * 100.0 / counterBytesTotal);
      theLog.log(LogInfoDevel_(3003), "Stats: average block size = %llu bytes",
                 (unsigned long long)counterBytesTotal / counterBlocks);
      theLog.log(LogInfoDevel_(3003), 
          "Stats: average block rate = %s",
          NumberOfBytesToString((counterBlocks) / elapsedTime, "Hz", 1000)
              .c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: average throughput = %s",
                 NumberOfBytesToString(counterBytesTotal / elapsedTime, "B/s")
                     .c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: average throughput = %s",
                 NumberOfBytesToString(counterBytesTotal * 8 / elapsedTime,
                                       "bits/s", 1000)
                     .c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: elapsed time = %.5lfs", elapsedTime);
    } else {
      theLog.log(LogInfoDevel_(3003), "Stats: no data received");
    }

    Consumer::stop();
    return 0;
  };
};

std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile &cfg,
                                                 std::string cfgEntryPoint) {
  return std::make_unique<ConsumerStats>(cfg, cfgEntryPoint);
}
