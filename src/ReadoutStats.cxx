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
/// @file    ReadoutStats.cxx
/// @author  Sylvain Chapeland
///

// instanciation of a global instance for readout counters
// to be updated by the relevant modules

#include "ReadoutStats.h"

#include "readoutInfoLogger.h"
#include "DataBlock.h"
#include <string.h>
#include <Common/Timer.h>

// the global stats instance
ReadoutStats gReadoutStats;

ReadoutStats::ReadoutStats() { 
  counters.version = ReadoutStatsCountersVersion;
  bzero(&counters.source, sizeof(counters.source));
  reset();

  shutdownThread = 0;
  std::function<void(void)> l = std::bind(&ReadoutStats::threadLoop, this);
  publishThread = std::make_unique<std::thread>(l);
}

ReadoutStats::~ReadoutStats() {
  if (publishThread != nullptr) {
    shutdownThread = 1;
    publishThread->join();
    publishThread = nullptr;
  }
  zmqCleanup();
}

void ReadoutStats::reset(bool lightReset)
{
  counters.notify = 0;
  
  counters.numberOfSubtimeframes = 0;
  counters.bytesReadout = 0;
  counters.bytesRecorded = 0;
  counters.bytesFairMQ = 0;

  counters.timestamp = 0;
  counters.bytesReadoutRate = 0;
  counters.state = 0;

  counters.pagesPendingFairMQ = 0;
  counters.pagesPendingFairMQreleased = 0;
  counters.pagesPendingFairMQtime = 0;
  counters.timeframeIdFairMQ = 0;

  counters.firstOrbit = undefinedOrbit;
  counters.currentOrbit = undefinedOrbit;

  if (!lightReset) {
    for (unsigned int i = 0; i < ReadoutStatsMaxItems; i++) {
      counters.bufferUsage[i] = -1.0;
    }
  }
}

void ReadoutStats::print()
{
  theLog.log(LogInfoSupport_(3003), "Readout global stats: numberOfSubtimeframes=%llu bytesReadout=%llu bytesRecorded=%llu bytesFairMQ=%llu",
             (unsigned long long)counters.numberOfSubtimeframes.load(),
             (unsigned long long)counters.bytesReadout.load(),
             (unsigned long long)counters.bytesRecorded.load(),
             (unsigned long long)counters.bytesFairMQ.load());
}

uint64_t stringToUint64(const char* in)
{
  char res[8] = { 0 };
  strncpy(res, in, sizeof(res) - 1);
  return *((uint64_t*)res);
}

int ReadoutStats::startPublish(const std::string &cfgZmqPublishAddress, double cfgZmqPublishInterval) {

  if (zmqEnabled) return __LINE__;

  #ifdef WITH_ZMQ

  // default ZMQ settings for data monitoring
  int cfg_ZMQ_CONFLATE = 0; // buffer last message only
  int cfg_ZMQ_IO_THREADS = 1; // number of IO threads
  int cfg_ZMQ_LINGER = 1000; // close timeout
  int cfg_ZMQ_SNDBUF = 32*1024; // kernel transmit buffer size
  int cfg_ZMQ_SNDHWM = 10; // max send queue size
  int cfg_ZMQ_SNDTIMEO = 2000; // send timeout

  if (cfgZmqPublishAddress != "") {

    //theLog.log(LogInfoDevel_(3002), "ReadoutStats publishing enabled - using %s period %fs", cfgZmqPublishAddress.c_str(), cfgZmqPublishInterval);

    int linerr = 0;
    int zmqerr = 0;
    publishMutex.lock();
    for (;;) {
      zmqContext = zmq_ctx_new();
      if (zmqContext == nullptr) {
        linerr = __LINE__;
        zmqerr = zmq_errno();
        break;
      }

      zmq_ctx_set(zmqContext, ZMQ_IO_THREADS, cfg_ZMQ_IO_THREADS);
      if (zmq_ctx_get(zmqContext, ZMQ_IO_THREADS) != cfg_ZMQ_IO_THREADS) {
        linerr = __LINE__;
        break;
      }
      zmqHandle = zmq_socket(zmqContext, ZMQ_PUSH);
      if (zmqHandle==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
      zmqerr = zmq_setsockopt(zmqHandle, ZMQ_CONFLATE, &cfg_ZMQ_CONFLATE, sizeof(cfg_ZMQ_CONFLATE));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zmqHandle, ZMQ_LINGER, (void*)&cfg_ZMQ_LINGER, sizeof(cfg_ZMQ_LINGER));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zmqHandle, ZMQ_SNDBUF, (void*)&cfg_ZMQ_SNDBUF, sizeof(cfg_ZMQ_SNDBUF));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr=zmq_setsockopt(zmqHandle, ZMQ_SNDHWM, &cfg_ZMQ_SNDHWM, sizeof(cfg_ZMQ_SNDHWM));
      if (zmqerr) { linerr=__LINE__; break; }
      zmqerr = zmq_setsockopt(zmqHandle, ZMQ_SNDTIMEO, (void*)&cfg_ZMQ_SNDTIMEO, sizeof(cfg_ZMQ_SNDTIMEO));
      if (zmqerr) { linerr=__LINE__; break; }

      zmqerr = zmq_connect(zmqHandle, cfgZmqPublishAddress.c_str());
      if (zmqerr) { linerr=__LINE__; break; }
      
      publishInterval = cfgZmqPublishInterval;
      zmqEnabled = 1;

      break;
    }
    publishMutex.unlock();

    if ((zmqerr) || (linerr)) {
      theLog.log(LogErrorSupport_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      // theLog.log(LogErrorDevel, "ReadoutStats publishing disabled");
      zmqCleanup();
      return __LINE__;
    }
  } else {
    return -1; //disabled
  }
  #endif

  publishNow();
  return 0;
}

#ifdef WITH_ZMQ
void ReadoutStats::zmqCleanup() {
  publishMutex.lock();
  if (zmqHandle != nullptr) {
    zmq_close(zmqHandle);
    zmqHandle = nullptr;
  }
  if (zmqContext != nullptr) {
    zmq_ctx_destroy(zmqContext);
    zmqContext = nullptr;
  }
  zmqEnabled = 0;
  publishMutex.unlock();
}
#endif


int ReadoutStats::stopPublish() {
  publishNow();
  zmqCleanup();
  return 0;
}

void ReadoutStats::threadLoop() {

  // loop period in microseconds
  unsigned int loopPeriod = (unsigned int) 100000;
  unsigned long long t0 = 0;
  unsigned int periodCount = 0;
  
  for(;shutdownThread.load() == 0;) {
  
    // align it to system clock seconds
    unsigned int sleeptime = loopPeriod - ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())).count() % loopPeriod);
    std::this_thread::sleep_for(std::chrono::microseconds(sleeptime));
    
    unsigned long long microseconds = (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())).count();
        
    // compute rates on last interval
    unsigned long long dt = microseconds - t0;
    if (t0) {
	counters.bytesReadoutRate = counters.bytesReadout / dt;
    }
    t0 = microseconds;
  
    counters.logMessages=theLog.getMessageCount(InfoLogger::Severity::Undefined);
    counters.logMessagesWarning=theLog.getMessageCount(InfoLogger::Severity::Warning);
    counters.logMessagesError=theLog.getMessageCount(InfoLogger::Severity::Error);
  
    // publish at specified interval
    periodCount++;
    if ( (periodCount * loopPeriod / 1000000.0) >= publishInterval) {
      publishNow();
      periodCount = 0;
    }
    
  }
  // final publish on exit
  publishNow();
}

void ReadoutStats::publishNow() {
  #ifdef WITH_ZMQ
  if (zmqEnabled) {
    uint64_t newUpdate = counters.notify.load();
    ReadoutStatsCounters snapshot;
    memcpy((void *)&snapshot, (void *)&gReadoutStats.counters, sizeof(snapshot));
    snapshot.timestamp = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())).count())/1000000.0;
    publishMutex.lock();
    if (zmqHandle != nullptr) {
      if ((newUpdate != lastUpdate) || (snapshot.timestamp.load()-lastPublishTimestamp > publishInterval - 0.1)) {
	zmq_send(zmqHandle, &snapshot, sizeof(snapshot), ZMQ_DONTWAIT);
	lastUpdate = newUpdate;
	lastPublishTimestamp = snapshot.timestamp;
      }
    }
    publishMutex.unlock();
  }
 #endif
}
