// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef _COUNTERSTATS_H
#define _COUNTERSTATS_H

#include <stdint.h>
#include <vector>

typedef uint64_t CounterValue;

class CounterStats {
public:
  CounterStats();
  ~CounterStats();

  // do not mix set() and increment()

  void set(CounterValue value);           // assign new value
  void increment(CounterValue value = 1); // increment new value
  void reset();                           // restore all stats to zero

  CounterValue get();      // get latest value
  CounterValue getTotal(); // get total of previous values set
  double getAverage();
  CounterValue getMinimum();
  CounterValue getMaximum();
  CounterValue getCount();

  void enableHistogram(unsigned int nbins, CounterValue vmin, CounterValue vmax, int logScale = 1);
  void getHisto(std::vector<double> &x, std::vector<CounterValue> &count);

private:
  CounterValue value; // last value set

  // derived statistics
  CounterValue sum;     // sum of previous values set (to compute average)
  CounterValue nValues; // number of time value was set (to compute average)
  CounterValue min;     // minimum value set
  CounterValue max;     // maximum value set

  void updateStats(); // update statistics from latest value

  std::vector<CounterValue> histoCounts; // store record of registered values
  CounterValue histoVmin;                // min value in histogram
  CounterValue histoVmax;                // max value in histogram
  unsigned int histoNbin;                // number of steps in histo
  double histoStep;                      // step size (as a fraction of vmax)
  double histoK1;                        // scaling factor
  double histoK2;                        // scaling factor
  int histoLogScale;                     // if set, using logarithmic scale on X
};

#endif // #ifndef _COUNTERSTATS_H
