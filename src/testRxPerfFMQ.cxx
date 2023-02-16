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

// test receiver program to benchmark FMQ interprocess communication

#include <Common/Timer.h>
#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>
#include <stdio.h>
#include <vector>

#include "ReadoutUtils.h"

int main()
{

  std::string cfgTransportType = "shmem";
  std::string cfgChannelName = "test";
  std::string cfgChannelType = "pair";
  std::string cfgChannelAddress = "ipc:///tmp/test-pipe";

  auto factory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{ cfgChannelName, cfgChannelType, factory };
  pull.Connect(cfgChannelAddress);
  int64_t ret;

  printf("Starting FMQ multi-part receiver '%s' %s %s @ %s\n", cfgChannelName.c_str(), cfgTransportType.c_str(), cfgChannelType.c_str(), cfgChannelAddress.c_str());

  int statInterval = 1; // interval between stats, in seconds
  AliceO2::Common::Timer timerStats;
  AliceO2::Common::Timer runningTime;
  timerStats.reset(1000000.0 * statInterval);

  size_t msgCount = 0;
  size_t msgNew = 0;
  size_t msgNewPart = 0;
  //double tNew = 0.0;
  double utimeNew = 0.0;
  double stimeNew = 0.0;
  bool isFirstStat = 1;
  bool isMultiPart = 1;

  double txCPU = 0;

  for (;;) {
    std::vector<FairMQMessagePtr> msgs;
    FairMQParts msgParts;

    if (isMultiPart) {
      ret = pull.Receive(msgParts, statInterval * 1000);
    } else {
      ret = pull.Receive(msgs, statInterval * 1000);
    }

    if (timerStats.isTimeout()) {
      double t = timerStats.getTime();
      //double tt = runningTime.getTime();
      double uTime, sTime;
      double uTimePercent = -1, sTimePercent = -1;
      if (getProcessStats(uTime, sTime) == 0) {
        uTimePercent = (uTime - utimeNew) * 100.0 / t;
        sTimePercent = (sTime - stimeNew) * 100.0 / t;
        stimeNew = sTime;
        utimeNew = uTime;
      }
      if (isFirstStat) {
        isFirstStat = 0;
        printf("Interval   Messages       Rate      Parts       Rate     ------- Rx CPU used --------      Tx CPU    Total\n");
        printf("       s        msg         Hz        msg         Hz     total %%   user %%  system %%             %%      msg\n");
      }
      printf("%8.1f   %8lu   %8.1lf   %8lu   %8.1lf    %8.1lf %8.1lf  %8.1lf        %6.0lf %8lu\n", t, msgNew, msgNew / t, msgNewPart, msgNewPart / t, uTimePercent + sTimePercent, uTimePercent, sTimePercent, txCPU, msgCount);
      msgNew = 0;
      msgNewPart = 0;
      while (timerStats.isTimeout()) {
        timerStats.increment();
      }
    }

    if (ret > 0) {
      if (isMultiPart) {
        msgCount++;
        msgNew++;
        int nPart = msgParts.Size();
        msgNewPart += nPart;
        for (auto const& mm : msgParts) {
          txCPU = ((size_t *)mm->GetData())[0];
          break;
        }

      } else {
        msgCount += msgs.size();
        msgNew += msgs.size();
        /*
       for (auto& msg : msgs) {
          int sz = (int)msg->GetSize();
          void* data = msg->GetData();
          printf("Received message %p size %d\n", data, sz);
          printf("Releasing message %p size %d\n", data, sz);
        }
       */
      }
    }
  }

  return 0;
}

