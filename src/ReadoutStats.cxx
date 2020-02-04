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

// get global log handle
#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

// the global stats instance
ReadoutStats gReadoutStats;

ReadoutStats::ReadoutStats() { reset(); }

ReadoutStats::~ReadoutStats() {}

void ReadoutStats::reset() {
  numberOfSubtimeframes = 0;
  bytesReadout = 0;
  bytesRecorded = 0;
  bytesFairMQ = 0;
}

void ReadoutStats::print() {
  theLog.log("Readout global stats: numberOfSubtimeframes=%llu  "
             "bytesReadout=%llu bytesRecorded=%llu bytesFairMQ=%llu",
             (unsigned long long)numberOfSubtimeframes.load(),
             (unsigned long long)bytesReadout.load(),
             (unsigned long long)bytesRecorded.load(),
             (unsigned long long)bytesFairMQ.load());
}
