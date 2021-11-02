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

// the global stats instance
ReadoutStats gReadoutStats;

ReadoutStats::ReadoutStats() { 
  counters.version = ReadoutStatsCountersVersion;
  bzero(&counters.source, sizeof(counters.source));
  reset();
}

ReadoutStats::~ReadoutStats() {
  stopPublish();
}

void ReadoutStats::reset()
{
  counters.notify = 0;
  
  counters.numberOfSubtimeframes = 0;
  counters.bytesReadout = 0;
  counters.bytesRecorded = 0;
  counters.bytesFairMQ = 0;

  counters.timestamp = time(nullptr);
  counters.bytesReadoutRate = 0;
  counters.state = 0;

  counters.pagesPendingFairMQ = 0;
  counters.pagesPendingFairMQreleased = 0;
  counters.pagesPendingFairMQtime = 0;
  counters.timeframeIdFairMQ = 0;

  counters.firstOrbit = undefinedOrbit;
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

int ReadoutStats::startPublish() {
  if (publishThread != nullptr) {
    return -1;
  }

  #ifdef WITH_ZMQ

  // default ZMQ settings for data monitoring
  int cfg_ZMQ_CONFLATE = 0; // buffer last message only
  int cfg_ZMQ_IO_THREADS = 1; // number of IO threads
  int cfg_ZMQ_LINGER = 1000; // close timeout
  int cfg_ZMQ_SNDBUF = 32*1024; // kernel transmit buffer size
  int cfg_ZMQ_SNDHWM = 10; // max send queue size
  int cfg_ZMQ_SNDTIMEO = 2000; // send timeout

  std::string cfgZmqPublishAddress="tcp://127.0.0.1:6008";
  if (cfgZmqPublishAddress != "") {
    theLog.log(LogInfoDevel_(3002), "ReadoutStats ZMQ publishing enabled - using %s", cfgZmqPublishAddress.c_str());

    int linerr = 0;
    int zmqerr = 0;
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

      zmqEnabled = 1;

      break;
    }

    if ((zmqerr) || (linerr)) {
      theLog.log(LogErrorSupport_(3236), "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      theLog.log(LogErrorDevel, "ReadoutStats ZMQ publishing disabled");
    }
  }
  #endif

  publishNow();

  shutdownThread = 0;
  std::function<void(void)> l = std::bind(&ReadoutStats::threadLoop, this);
  publishThread = std::make_unique<std::thread>(l);  
  return 0;
}

int ReadoutStats::stopPublish() {
  publishNow();

  if (publishThread != nullptr) {
    shutdownThread = 1;
    publishThread->join();
    publishThread = nullptr;
  }
  
  zmqEnabled = 0;
  return 0;
}

void ReadoutStats::threadLoop() {
  for(;shutdownThread.load() == 0;) {
    publishNow();
    usleep(1000000);
  }
}

void ReadoutStats::publishNow() {
  #ifdef WITH_ZMQ
  if (zmqEnabled) {
    fflush(stdout);
    uint64_t newUpdate = counters.notify.load();
    ReadoutStatsCounters snapshot;
    memcpy((void *)&snapshot, (void *)&gReadoutStats.counters, sizeof(snapshot));
    snapshot.timestamp = time(NULL);
    mutex.lock();
    if (newUpdate != lastUpdate) {
      zmq_send(zmqHandle, &snapshot, sizeof(snapshot), ZMQ_DONTWAIT);
      lastUpdate = newUpdate;
    }   
    mutex.unlock();
  }
 #endif
}
