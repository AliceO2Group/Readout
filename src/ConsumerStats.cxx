// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <Common/Timer.h>
#include <Monitoring/MonitoringFactory.h>
#include <math.h>
#include <memory>
#include <sys/resource.h>
#include <sys/time.h>
#include <unordered_map>

#include "Consumer.h"
#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "ReadoutUtils.h"
#include "ReadoutStats.h"

using namespace o2::monitoring;

#ifdef WITH_ZMQ
#include <zmq.h>
#endif

extern tRunNumber occRunNumber;

class ConsumerStats : public Consumer
{
 private:
  uint64_t counterBlocks;
  uint64_t counterBlocksDiff;
  uint64_t counterBytesTotal;
  uint64_t counterBytesHeader;
  uint64_t counterBytesDiff;
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer monitoringUpdateTimer;
  double elapsedTime;       // value used for rates computation
  double intervalStartTime; // counter for interval statistics, keeps last timestamp

  int monitoringEnabled;
  double monitoringUpdatePeriod;
  std::unique_ptr<Monitoring> monitoringCollector;
  int consoleUpdate; // if set, stats will be published also on console

  struct rusage previousUsage;    // variable to keep track of last getrusage() result
  struct rusage currentUsage;     // variable to keep track of last getrusage() result
  double cpuUsedOverLastInterval; // average CPU usage over latest measurement interval

  // per-equipment statistics
  struct EquipmentStats {
    uint64_t counterBytesPayload = 0;
  };
  typedef std::unordered_map<uint16_t, EquipmentStats> EquipmentStatsMap;
  EquipmentStatsMap equipmentStatsMap;

  bool isRunning = false;

// zeroMQ publish
#ifdef WITH_ZMQ
  void* zmqContext = nullptr;
  void* zmqHandle = nullptr;
  bool zmqEnabled = 0;
#endif

  // reset all counters and timers for a fresh start
  // must be called once before first publishStats() call
  void reset()
  {
    counterBlocks = 0;
    counterBlocksDiff = 0;
    counterBytesTotal = 0;
    counterBytesHeader = 0;
    counterBytesDiff = 0;
    elapsedTime = 0.0;
    intervalStartTime = 0;
    cpuUsedOverLastInterval = 0.0;
    equipmentStatsMap.clear();
    monitoringUpdateTimer.reset(monitoringUpdatePeriod * 1000000);
    runningTime.reset();
  }

  void sendMetricNoException(Metric&& metric, DerivedMetricMode mode = DerivedMetricMode::NONE)
  {
    try {
      monitoringCollector->send(std::forward<Metric&&>(metric), mode);
    } catch (...) {
      theLog.log(LogErrorDevel_(3234), "monitoringCollector->send(%s) failed", metric.getName().c_str());
    }
  }

  void publishStats()
  {
    // time for current interval
    double now = runningTime.getTime();
    double deltaT = 0.0;  // time elapsed since last call, in seconds
    if (intervalStartTime) {
      deltaT = now - intervalStartTime;
    }
    intervalStartTime = now;

    // fraction CPU used	
    getrusage(RUSAGE_SELF, &currentUsage);
    if (deltaT > 0) {
      double fractionCpuUsed = (currentUsage.ru_utime.tv_sec * 1000000.0 + currentUsage.ru_utime.tv_usec - (previousUsage.ru_utime.tv_sec * 1000000.0 + previousUsage.ru_utime.tv_usec) + currentUsage.ru_stime.tv_sec * 1000000.0 + currentUsage.ru_stime.tv_usec - (previousUsage.ru_stime.tv_sec * 1000000.0 + previousUsage.ru_stime.tv_usec)) / (deltaT * 1000000.0);
      if (monitoringEnabled) {
        sendMetricNoException({ fractionCpuUsed * 100, "readout.percentCpuUsed" });
        // theLog.log(LogDebugTrace,"CPU used = %.2f %%",100*fractionCpuUsed);
      }
    }
    previousUsage = currentUsage;
    // todo: per thread? -> add feature in Thread class

    // snapshot of current counters
    ReadoutStatsCounters snapshot;
    memcpy((void *)&snapshot, (void *)&gReadoutStats.counters, sizeof(snapshot));
    snapshot.timestamp = time(NULL);
    gReadoutStats.counters.pagesPendingFairMQtime = 0;
    gReadoutStats.counters.pagesPendingFairMQreleased = 0;
    gReadoutStats.counters.ddBytesCopied = 0;
    gReadoutStats.counters.ddHBFRepacked = 0;
    gReadoutStats.counters.notify++;
    unsigned long long nRfmq = snapshot.pagesPendingFairMQreleased.load();
    int tfidfmq = (int)snapshot.timeframeIdFairMQ.load();
    double avgTfmq = 0.0;
    double rRfmq = 0.0;
    double ddBytesCopiedRate = 0; // copy rate in MB/s
    double ddHBFRepackedRate = 0; // repack rate in Hz
    double ddMemoryEfficiency = 0; // memory efficiency in %

    if (nRfmq) {
      avgTfmq = (snapshot.pagesPendingFairMQtime.load() / nRfmq) / (1000000.0);
    }
    if (deltaT > 0) {
      rRfmq = nRfmq / deltaT;
      ddBytesCopiedRate = snapshot.ddBytesCopied / (1024*1024*deltaT);
      ddHBFRepackedRate = snapshot.ddHBFRepacked / deltaT;
    }
    if ((snapshot.ddPayloadPendingBytes > 0) && (snapshot.ddMemoryPendingBytes > 0)) {
      ddMemoryEfficiency = snapshot.ddPayloadPendingBytes * 100.0 / snapshot.ddMemoryPendingBytes;
    }
    
    if (monitoringEnabled) {
      // todo: support for long long types
      // https://alice.its.cern.ch/jira/browse/FLPPROT-69

      sendMetricNoException({ counterBlocks, "readout.Blocks" });
      // sendMetricNoException({counterBytesTotal, "readout.BytesTotal"});
      sendMetricNoException({ counterBytesTotal, "readout.BytesTotal" }, DerivedMetricMode::RATE);
      sendMetricNoException({ counterBytesDiff, "readout.BytesInterval" });
      // sendMetricNoException({(counterBytesTotal/(1024*1024)), "readout.MegaBytesTotal"});

      // per-equipment stats
      for (auto& it : equipmentStatsMap) {
	sendMetricNoException(Metric{it.second.counterBytesPayload, "readout.BytesEquipment"}.addTag(tags::Key::ID, (unsigned int)it.first), DerivedMetricMode::RATE);
      }
      
      // FMQ stats
      sendMetricNoException({ (int)snapshot.pagesPendingFairMQ.load(), "readout.stfbMemoryPagesLocked"});
      sendMetricNoException({ rRfmq, "readout.stfbMemoryPagesReleaseRate"});
      sendMetricNoException({ avgTfmq, "readout.stfbMemoryPagesReleaseLatency"});
      sendMetricNoException({ tfidfmq, "readout.stfbTimeframeId"});
      sendMetricNoException({ ddBytesCopiedRate, "readout.stfbHBFCopyRate"});
      sendMetricNoException({ ddHBFRepackedRate, "readout.stfbHBFRepackedRate"});
      sendMetricNoException({ ddMemoryEfficiency, "readout.stfbMemoryEfficiency"});
      sendMetricNoException({ snapshot.ddPayloadPendingBytes, "readout.stfbDataBytesLocked"});
      sendMetricNoException({ snapshot.ddMemoryPendingBytes, "readout.stfbMemoryBytesLocked"});

      // buffer stats
      for (int i = 0; i < ReadoutStatsMaxItems; i++) {
	double r = snapshot.bufferUsage[i].load();
        uint64_t b = (uint64_t)(r * snapshot.bufferSize[i].load());
	if (r >= 0) {
	  sendMetricNoException(Metric{"readout.bufferUsage"}.addValue((int)(r*100), "value").addValue(b, "bytes").addTag(tags::Key::ID, i));
	}
      }
    }

#ifdef WITH_ZMQ
    if (zmqEnabled) {
      gReadoutStats.counters.timestamp = time(NULL);
      zmq_send(zmqHandle, &snapshot, sizeof(snapshot), ZMQ_DONTWAIT);
    }
#endif

    if (consoleUpdate) {
      if (deltaT > 0) {
        theLog.log(LogInfoOps_(3003), "Last interval (%.2fs): blocksRx=%llu, block rate=%.2lf, block size = %.1lfkB, bytesRx=%llu, rate=%s", deltaT, (unsigned long long)counterBlocksDiff, counterBlocksDiff / deltaT, counterBytesDiff / (1024.0*counterBlocksDiff), (unsigned long long)counterBytesDiff, NumberOfBytesToString(counterBytesDiff * 8 / deltaT, "b/s", 1000).c_str());
	if (gReadoutStats.isFairMQ) {
          theLog.log(LogInfoOps_(3003), "STFB locked pages: current=%llu, released = %llu, release rate=%.2lf Hz, latency=%.3lf s, current TF = %d", (unsigned long long) snapshot.pagesPendingFairMQ.load(), nRfmq, rRfmq, avgTfmq, tfidfmq );
	  theLog.log(LogInfoOps_(3003), "STFB HBF repacking = %.1lf Hz, copy overhead = %.1lf MB/s = %.2f%%", ddHBFRepackedRate, ddBytesCopiedRate, ddBytesCopiedRate * 1024.0 * 1024.0 * 100.0 * deltaT / counterBytesDiff);
	  theLog.log(LogInfoOps_(3003), "STFB memory efficiency = %.1lf %%, data buffered = %.1lf MB, real memory used %.1lf MB",
	    ddMemoryEfficiency, snapshot.ddPayloadPendingBytes / (1024.0*1024.0), snapshot.ddMemoryPendingBytes / (1024.0*1024.0) );
	}
	std::string bufferReport;
	for (int i = 0; i < ReadoutStatsMaxItems; i++) {
	  double r = snapshot.bufferUsage[i].load();
	  if (r >= 0) {
	    bufferReport += "["+ std::to_string(i) + "]=" + std::to_string((int)(r*100)) + "% ";
	  }
	}
	if (bufferReport.length()) {
	  theLog.log(LogInfoOps_(3003), "Memory buffers usage: %s", bufferReport.c_str());
	}
	//theLog.log(LogInfoOps_(3003), "orbit=0x%X", (unsigned int) gReadoutStats.counters.currentOrbit.load());
      }
    }

    counterBytesDiff = 0;
    counterBlocksDiff = 0;
  }

  // run a function in a separate thread to publish data regularly, until flag isShutdown is set
  std::unique_ptr<std::thread> periodicUpdateThread; // the thread running periodic updates
  bool periodicUpdateThreadShutdown;                 // flag to stop periodicUpdateThread
  void periodicUpdate()
  {
    setThreadName("consumer-stats");
    periodicUpdateThreadShutdown = 0;

    // periodic update
    for (; !periodicUpdateThreadShutdown;) {
      if (!isRunning) {
        usleep(100000);
        continue;
      }

      double timeUntilTimeout = monitoringUpdateTimer.getRemainingTime(); // measured in seconds
      if (timeUntilTimeout <= 0) {
        publishStats();
        monitoringUpdateTimer.increment();
      } else {
        if (timeUntilTimeout > 1) {
          timeUntilTimeout = 1.0; // avoid a sleep longer than 1s to allow exiting promptly
        }
        usleep((int)(timeUntilTimeout * 1000000));
      }
    }
  }

 public:
  ConsumerStats(ConfigFile& cfg, std::string cfgEntryPoint) : Consumer(cfg, cfgEntryPoint)
  {

    // configuration parameter: | consumer-stats-* | monitoringEnabled | int | 0 | Enable (1) or disable (0) readout monitoring. |
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringEnabled", monitoringEnabled, 0);
    // configuration parameter: | consumer-stats-* | monitoringUpdatePeriod | double | 10 | Period of readout monitoring updates, in seconds. |
    cfg.getOptionalValue(cfgEntryPoint + ".monitoringUpdatePeriod", monitoringUpdatePeriod, 10.0);

    if (monitoringEnabled) {
      // configuration parameter: | consumer-stats-* | monitoringURI | string | | URI to connect O2 monitoring service. c.f. o2::monitoring. |
      const std::string configURI = cfg.getValue<std::string>(cfgEntryPoint + ".monitoringURI");

      theLog.log(LogInfoDevel_(3002), "Monitoring enabled - period %.2fs - using %s", monitoringUpdatePeriod, configURI.c_str());
      monitoringCollector = MonitoringFactory::Get(configURI.c_str());
      monitoringCollector->addGlobalTag(tags::Key::Subsystem, tags::Value::Readout);

      // enable process monitoring
      // configuration parameter: | consumer-stats-* | processMonitoringInterval | int | 0 | Period of process monitoring updates (O2 standard metrics). If zero (default), disabled.|
      int processMonitoringInterval = 0;
      cfg.getOptionalValue(cfgEntryPoint + ".processMonitoringInterval", processMonitoringInterval, 0);
      if (processMonitoringInterval > 0) {
        monitoringCollector->enableProcessMonitoring(processMonitoringInterval, {PmMeasurement::Cpu, PmMeasurement::Mem});
      }
    }

    // configuration parameter: | consumer-stats-* | consoleUpdate | int | 0 | If non-zero, periodic updates also output on the log console (at rate defined in monitoringUpdatePeriod). If zero, periodic log output is disabled. |
    cfg.getOptionalValue(cfgEntryPoint + ".consoleUpdate", consoleUpdate, 0);
    if (consoleUpdate) {
      theLog.log(LogInfoDevel_(3002), "Periodic console statistics enabled");
    }

#ifdef WITH_ZMQ
    // configuration parameter: | consumer-stats-* | zmqPublishAddress | string | | If defined, readout statistics are also published periodically (at rate defined in monitoringUpdatePeriod) to a ZMQ server. Suggested value: tcp://127.0.0.1:6008 (for use by o2-readout-monitor). |
    std::string cfgZmqPublishAddress = "";
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".zmqPublishAddress", cfgZmqPublishAddress);
    if (cfgZmqPublishAddress != "") {
      theLog.log(LogInfoDevel_(3002), "ZMQ stats publishing enabled - using %s", cfgZmqPublishAddress.c_str());
      int zmqError = 0;
      try {
        zmqContext = zmq_ctx_new();
        if (zmqContext == nullptr) {
          zmqError = zmq_errno();
          throw __LINE__;
        }

        zmqHandle = zmq_socket(zmqContext, ZMQ_PUSH);
        if (zmqHandle == nullptr) {
          zmqError = zmq_errno();
          throw __LINE__;
        }

	const int cfgZmqLinger = 1000;
        zmqError = zmq_setsockopt(zmqHandle, ZMQ_LINGER, (void*)&cfgZmqLinger, sizeof(cfgZmqLinger)); // close timeout
        if (zmqError) {
          throw __LINE__;
        }
	
        zmqError = zmq_connect(zmqHandle, cfgZmqPublishAddress.c_str());
        if (zmqError) {
          throw __LINE__;
        }

        zmqEnabled = 1;

      } catch (int lineErr) {
        if (zmqError) {
          theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", lineErr, zmqError, zmq_strerror(zmqError));
        } else {
          theLog.log(LogErrorDevel, "Error @%d", lineErr);
        }
        // ZMQ unavailable does not cause consumer to fail starting
        theLog.log(LogErrorDevel, "ZMQ stats publishing disabled");
      }
    }
#endif

    // make sure to initialize all counters and timers
    reset();

    // start thread for periodic updates
    std::function<void(void)> l = std::bind(&ConsumerStats::periodicUpdate, this);
    periodicUpdateThread = std::make_unique<std::thread>(l);
  }

  ~ConsumerStats()
  {
    if (isRunning) {
      stop();
    }

    periodicUpdateThreadShutdown = 1;
    periodicUpdateThread->join();

#ifdef WITH_ZMQ
    if (zmqHandle != nullptr) {
      zmq_close(zmqHandle);
      zmqHandle = nullptr;
    }
    if (zmqContext != nullptr) {
      zmq_ctx_destroy(zmqContext);
      zmqContext = nullptr;
    }
#endif
  }

  int pushData(DataBlockContainerReference& b)
  {

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
        equipmentStatsMap.insert({ eqId, newStats });
      } else {
        // equipment found, update counters
        it->second.counterBytesPayload += b->getData()->header.dataSize;
      }
    }

    return 0;
  }

  int start()
  {
    Consumer::start();
    theLog.log(LogInfoDevel_(3006), "Starting stats clock");
    reset();

    if (monitoringEnabled) {
      // set run number
      // run number = 0 means not set.
      monitoringCollector->setRunNumber(occRunNumber);
    }

    // publish once on start
    publishStats();

    isRunning = true;
    return 0;
  };

  int stop()
  {
    isRunning = false;
    theLog.log(LogInfoDevel_(3006), "Stopping stats clock");
    elapsedTime = runningTime.getTime();

    // publish once more on stop
    publishStats();

    if (counterBytesTotal > 0) {
      theLog.log(LogInfoDevel_(3003), "Statistics for %s", this->name.c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: %llu blocks, %.2f MB, %.2f%% header overhead", (unsigned long long)counterBlocks, counterBytesTotal / (1024 * 1024.0), counterBytesHeader * 100.0 / counterBytesTotal);
      theLog.log(LogInfoDevel_(3003), "Stats: average block size = %llu bytes", (unsigned long long)counterBytesTotal / counterBlocks);
      theLog.log(LogInfoDevel_(3003), "Stats: average block rate = %s", NumberOfBytesToString((counterBlocks) / elapsedTime, "Hz", 1000).c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: average throughput = %s", NumberOfBytesToString(counterBytesTotal / elapsedTime, "B/s").c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: average throughput = %s", NumberOfBytesToString(counterBytesTotal * 8 / elapsedTime, "bits/s", 1000).c_str());
      theLog.log(LogInfoDevel_(3003), "Stats: elapsed time = %.5lfs", elapsedTime);
    } else {
      theLog.log(LogInfoDevel_(3003), "Stats: no data received");
    }

    Consumer::stop();
    return 0;
  };
};

std::unique_ptr<Consumer> getUniqueConsumerStats(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ConsumerStats>(cfg, cfgEntryPoint); }

