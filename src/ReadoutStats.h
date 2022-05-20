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

///
/// @file    ReadoutStats.h
/// @author  Sylvain Chapeland
///

// This defines a class to keep trakc of some readout counters,

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#ifdef WITH_ZMQ
#include <zmq.h>
#endif

const int ReadoutStatsMaxItems = 25;

struct ReadoutStatsCounters {
  uint32_t version; // version number of this header
  char source[32]; // name of the source providing these counters
  std::atomic<uint64_t> notify; // counter updated each time there is a change in the struct

  std::atomic<uint64_t> numberOfSubtimeframes;
  std::atomic<uint64_t> bytesReadout;
  std::atomic<uint64_t> bytesRecorded;
  std::atomic<uint64_t> bytesFairMQ;
   std::atomic<double> timestamp;
  std::atomic<double> bytesReadoutRate;
  std::atomic<uint64_t> state;
  std::atomic<uint64_t> pagesPendingFairMQ;         // number of pages pending in ConsumerFMQ
  std::atomic<uint64_t> pagesPendingFairMQreleased; // number of pages which have been released by ConsumerFMQ
  std::atomic<uint64_t> pagesPendingFairMQtime;     // latency in FMQ, in microseconds, total for all released pages
  std::atomic<uint32_t> timeframeIdFairMQ;          // last timeframe pushed to ConsumerFMQ
  std::atomic<uint32_t> firstOrbit;                 // value of first orbit received
  std::atomic<uint32_t> logMessages;                // number of log messages (severity: any)
  std::atomic<uint32_t> logMessagesWarning;         // number of log messages (severity: warning)
  std::atomic<uint32_t> logMessagesError;           // number of log messages (severity: error)
  std::atomic<uint32_t> currentOrbit;               // 1st orbit of current timeframe (last out of aggregator)
  std::atomic<double> bufferUsage[ReadoutStatsMaxItems]; // buffer usage. -1 means not used.
};

// version number of this struct
const uint32_t ReadoutStatsCountersVersion = 0xA0000002;

// need to be able to easily transmit this struct as a whole
static_assert(std::is_pod<ReadoutStatsCounters>::value);

// utility to assign strings to uint64
uint64_t stringToUint64(const char*);

class ReadoutStats
{
 public:
  ReadoutStats();
  ~ReadoutStats();
  void reset(bool lightReset=0); // light reset: to preserve the buffer counters
  void print();

  ReadoutStatsCounters counters;
  bool isFairMQ; // flag to report when FairMQ used
  
  int startPublish(const std::string &cfgZmqPublishAddress, double cfgZmqPublishInterval);
  int stopPublish();
  void publishNow();

  std::mutex mutex; // a general purpose lock for shared counters, eg to set firstOrbit

 private:
  std::unique_ptr<std::thread> publishThread;
  std::atomic<int> shutdownThread = 0;
  void threadLoop();
  double publishInterval;
  std::mutex publishMutex;
    
  #ifdef WITH_ZMQ
  void* zmqContext = nullptr;
  void* zmqHandle = nullptr;
  bool zmqEnabled = 0;
  void zmqCleanup();
  #endif
  uint64_t lastUpdate = (uint64_t)-1;
  double lastPublishTimestamp = 0;
};

extern ReadoutStats gReadoutStats;

