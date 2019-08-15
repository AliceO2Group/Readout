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

class ReadoutStats {
public:
  ReadoutStats();
  ~ReadoutStats();
  void reset();
  void print();

  std::atomic<uint64_t> numberOfSubtimeframes;
  std::atomic<uint64_t> bytesReadout;
  std::atomic<uint64_t> bytesRecorded;
  std::atomic<uint64_t> bytesFairMQ;
};

extern ReadoutStats gReadoutStats;
