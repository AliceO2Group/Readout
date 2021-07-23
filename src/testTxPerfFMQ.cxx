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

// test sender program to benchmark FMQ interprocess communication

#include <Common/Timer.h>
#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>
#include <stdio.h>

#include "ReadoutUtils.h"

int main()
{

  std::string cfgTransportType = "shmem";
  std::string cfgChannelName = "test";
  std::string cfgChannelType = "pair";
  std::string cfgChannelAddress = "ipc:///tmp/test-pipe";

  auto transportFactory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto channel = FairMQChannel{ cfgChannelName, cfgChannelType, transportFactory };
  channel.Bind(cfgChannelAddress);
  if (!channel.Validate()) {
    return -1;
  }

  const size_t bufferSize = 2000 * 1024L * 1024L;
  auto memoryBuffer = channel.Transport()->CreateUnmanagedRegion(bufferSize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {
    // cleanup callback
  });
  printf("Created buffer %p size %ld\n", memoryBuffer->GetData(), memoryBuffer->GetSize());

  int statInterval = 1; // interval between stats, in seconds
  AliceO2::Common::Timer runningTime;
  AliceO2::Common::Timer timerStats;

  size_t msgCount = 0;
  size_t msgMax = 10000;
  size_t msgSize = 100;

  size_t msgParts = 257;
  double msgRate = 3168;
  size_t sequenceTime = 15; // duration of each sequence

  char* buf = (char*)memoryBuffer->GetData();
  size_t ix = 0;

  double lastCPUu = 0;
  double lastCPUs = 0;
  double CPUt = 0;

  /*
    for (msgRate=10; msgRate<=1000000; msgRate*=10) {
    for (msgParts=1;msgParts<=1024;msgParts*=4) {
  */
  msgMax = msgRate * sequenceTime;

  msgCount = 0;
  runningTime.reset();
  timerStats.reset(1000000.0 * statInterval);

  getProcessStats(lastCPUu, lastCPUs);
  std::vector<FairMQMessagePtr> msgs;
  msgs.reserve(msgParts);

  printf("starting sequence for %lus : rate = %.0lfHz, %lu parts per message,\n", sequenceTime, msgRate, msgParts);
  for (size_t i = 0; i < msgMax; i++) {
    msgs.clear();

    for (size_t im = 0; im < msgParts; im++) {
      msgSize = 100 + (size_t)CPUt;
      void* dataPtr = (void*)(&buf[ix]);
      void* hint = (void*)i;
      ix += msgSize;
      if (ix >= bufferSize) {
        ix = 0;
      }
      msgs.emplace_back(channel.NewMessage(memoryBuffer, dataPtr, msgSize, hint));
    }
    channel.Send(msgs);
    msgCount++;

    for (;;) {
      //double now = runningTime.getTime();
      double rate = msgCount / runningTime.getTime();
      if (rate <= msgRate) {
        break;
      }
      usleep(1000000 / msgRate);
    }

    if (timerStats.isTimeout()) {
      double CPUu, CPUs;
      getProcessStats(CPUu, CPUs);
      CPUt = (CPUu - lastCPUu + CPUs - lastCPUs) * 100.0 / timerStats.getTime();
      lastCPUu = CPUu;
      lastCPUs = CPUs;

      printf("%lu -> CPU = %lf %%\n", msgCount, CPUt);
      timerStats.increment();
    }
  }
  printf("sequence completed\n");
  sleep(3);
  /*
    }
    }
  */

  return 0;
}

