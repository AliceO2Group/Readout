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
/// @file    ReadoutStats.h
/// @author  Sylvain Chapeland
///

// This defines a class to keep trakc of some readout counters,

#include <atomic>

struct ReadoutStatsCounters {
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
};

// need to be able to easily transmit this struct as a whole
static_assert(std::is_pod<ReadoutStatsCounters>::value);

// utility to assign strings to uint64
uint64_t stringToUint64(const char*);

class ReadoutStats
{
 public:
  ReadoutStats();
  ~ReadoutStats();
  void reset();
  void print();

  ReadoutStatsCounters counters;
};

extern ReadoutStats gReadoutStats;
