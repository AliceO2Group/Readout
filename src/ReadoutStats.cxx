// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
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

// the global stats instance
ReadoutStats gReadoutStats;

ReadoutStats::ReadoutStats() { reset(); }

ReadoutStats::~ReadoutStats() {}

void ReadoutStats::reset()
{
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
}

void ReadoutStats::print() {
  theLog.log(LogInfoSupport_(3003), "Readout global stats: numberOfSubtimeframes=%llu bytesReadout=%llu bytesRecorded=%llu bytesFairMQ=%llu",
    (unsigned long long)counters.numberOfSubtimeframes.load(),
    (unsigned long long)counters.bytesReadout.load(),
    (unsigned long long)counters.bytesRecorded.load(),
    (unsigned long long)counters.bytesFairMQ.load());
}

uint64_t stringToUint64(const char *in) {
  char res[8] = {0};
  strncpy(res, in, sizeof(res)-1);
  return *((uint64_t *)res);
}
