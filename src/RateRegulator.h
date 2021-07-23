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

#include <chrono>
#include <math.h>

// helper class to count items and check compliance with a rate limit
// optimized to be lightweight (high rates) and precise over time
// usage:
//   - initialize with target rate
//   - call next() and check return value, to know if within rate. each successed call count for one in the rate average since init.

class RateRegulator {
public:

  // initialize with a given maximum rate (Hertz)
  RateRegulator(double maxRate = 0) {
    init(maxRate);
  }
  
  // destructor
  ~RateRegulator() {
  }
  
  void init(double maxRate) {
    if (maxRate>0) {
      nolimit = 0;
      period = 1000000.0 / maxRate; // period in microseconds (full precision)
      tperiod = std::chrono::microseconds((int)period); // period in microseconds (integral)    
      tnextFullRefreshInterval = (unsigned int)(floor(maxRate)); // update with full precision no more than once a second
    } else {
      nolimit = 1; // no rate limit defined
    }
    reset();  
  }
  
  // validate a new item
  // checks if comply with maximum rate defined
  // returns:
  // - true if accepted (within rate limit)
  // - false if rejected (rate exceeded)
  bool next() {
    if (nolimit) {
      // shortcut when no rate limit defined
      return true;
    }
    if ( std::chrono::steady_clock::now() < tnext ) {
       // reject count disabled
       // nItemsRejected++;
       return false;
     }
     nItemsAccepted++;
     updateTimeNext();
     return true;
  };
  
  // reset status
  // this sets the initial time from which we count items and calculate rate limit
  void reset() {
    nItemsAccepted = 0;
    nItemsRejected = 0;
    t0 = std::chrono::steady_clock::now();
    tnext = t0;
    tnextCount = 1;
  }

  // update the minimal timestamp of next allowed item  
  void updateTimeNext() {
    if (tnextCount >= tnextFullRefreshInterval) {
      // full update, calculated from t0
      tnext = t0 + std::chrono::microseconds((int)(nItemsAccepted * period));
      tnextCount = 1;
    } else {
      // quick update, by incrementing period
      tnext += tperiod;
      tnextCount++;
    }
  }
  
  // return time remaining until next allowed item
  // in seconds
  // (can be negative, if already passed)
  double getTimeUntilNext() {
    std::chrono::duration<double> tdiff = tnext - std::chrono::steady_clock::now();
    return tdiff.count();
  }
  
private:
  unsigned long long nItemsAccepted; // number of items accepted (below rate)
  unsigned long long nItemsRejected; // number of items rejected (rate exceeded)
  std::chrono::time_point<std::chrono::steady_clock> t0; // start time
  std::chrono::time_point<std::chrono::steady_clock> tnext; // time of next item acceptance
  unsigned int tnextCount; // counter for period tnext refresh
  unsigned int tnextFullRefreshInterval; // do full calculation when tnextCount reaches this value
  double period = 0; // rate limit, converted to a period in microseconds (full precision)
  std::chrono::microseconds tperiod; // corresponding period, in integral microseconds - used for quick updates
  bool nolimit; // flag set when rate limit disabled
};

/*
  // some test code
  double t=0.5;
  RateRegulator r(t);
  int i=0;
  for(;;) {
    if (r.next()) {
      if (i==0) {
        theLog.log(LogInfoOps, "TICK");
      }
      i++;
       if (i==(int)ceil(t)) {       
         i=0;
       }
    } else {
      double s = r.getTimeUntilNext();
      printf("need to wait: %.6fs\n", s);
      usleep(s*1000000);
    }
  }

*/

