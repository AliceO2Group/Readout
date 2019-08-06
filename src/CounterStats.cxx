// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "CounterStats.h"

CounterStats::CounterStats() {
  reset();
}

void CounterStats::reset() {
  value=0;
  sum=0;
  nValues=0;
  min=UINT64_MAX;
  max=0;
}

void CounterStats::set(CounterValue newValue) {
  value=newValue;
  sum+=newValue;
  if (value<min) {
    min=value;
  }
  if (value>max) {
    max=value;
  }
  nValues++;
}

void CounterStats::increment(CounterValue increment) {
  value+=increment;
  sum+=increment; 
  if (increment<min) {
    min=increment;
  }
  if (increment>max) {
    max=increment;
  }
  nValues++;
}

CounterValue CounterStats::get(){
  return value;
}

CounterValue CounterStats::getTotal() {
  return sum;
}

double CounterStats::getAverage() {
  if (nValues) {
    return sum*1.0/nValues;
  } else {
    return 0;
  }
}

CounterValue CounterStats::getMinimum() {
  if (nValues) {
    return min;
  }
  return 0;
}

CounterValue CounterStats::getMaximum() {
  if (nValues) {
    return max;
  }
  return 0;
}
  
CounterValue CounterStats::getCount() {
  return nValues;
}
  
