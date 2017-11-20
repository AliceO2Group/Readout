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
 return min;
}

CounterValue CounterStats::getMaximum() {
  return max;
}
  
CounterValue CounterStats::getCount() {
  return nValues;
}
  
