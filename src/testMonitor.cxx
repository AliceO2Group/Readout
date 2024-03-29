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

// simple test program used to evaluate overhead of monitoring lib

#include <Monitoring/MonitoringFactory.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
using namespace o2::monitoring;

int main()
{

  std::unique_ptr<Monitoring> monitoringCollector;
  monitoringCollector = MonitoringFactory::Get("influxdb-udp://aido2mon-gpn.cern.ch:8088");
  monitoringCollector->enableProcessMonitoring(1);

  uint64_t bytesTotal = 0;

  for (;;) {
    bytesTotal += 1000000000;
    monitoringCollector->send({ bytesTotal, "readout.BytesTotal" });
    monitoringCollector->send({ bytesTotal, "readout.BytesTotal" }, DerivedMetricMode::RATE);
    printf(".");
    fflush(stdout);
    sleep(1);
  }

  return 0;
}

