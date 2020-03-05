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
#include <math.h>

CounterStats::CounterStats() { reset(); }
CounterStats::~CounterStats() {}

void CounterStats::reset() {
  value = 0;
  sum = 0;
  nValues = 0;
  min = UINT64_MAX;
  max = 0;
  histoNbin = 0;
  histoCounts.clear();
}

void CounterStats::set(CounterValue newValue) {
  value = newValue;
  sum += newValue;
  if (value < min) {
    min = value;
  }
  if (value > max) {
    max = value;
  }
  nValues++;
  if (histoNbin) {
    if (newValue <= histoVmin) {
      histoCounts[0]++;
    } else if (newValue >= histoVmax) {
      histoCounts[histoNbin - 1]++;
    } else {
      int n = 0;
      if (histoLogScale) {
        n = (int)floor(histoNbin - 1 - (log(newValue * histoK1) * histoK2));
      } else {
        n = (int)floor(1 + (newValue - histoVmin) * histoK1);
      }
      if ((n >= 0) && (n < (int)histoNbin)) {
        histoCounts[n]++;
      }
    }
  }
}

void CounterStats::increment(CounterValue increment) {
  value += increment;
  sum += increment;
  if (increment < min) {
    min = increment;
  }
  if (increment > max) {
    max = increment;
  }
  nValues++;
}

CounterValue CounterStats::get() { return value; }

CounterValue CounterStats::getTotal() { return sum; }

double CounterStats::getAverage() {
  if (nValues) {
    return sum * 1.0 / nValues;
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

CounterValue CounterStats::getCount() { return nValues; }

void CounterStats::enableHistogram(unsigned int nbin, CounterValue vmin,
                                   CounterValue vmax, int logScale) {
  histoCounts.clear();
  histoVmin = vmin;
  histoVmax = vmax;
  histoNbin = nbin;
  histoLogScale = logScale;
  if (nbin == 0) {
    histoStep = 0;
    return;
  }
  if (histoLogScale) {
    histoStep = exp(log(vmin * 1.0 / vmax) / (nbin - 1));
    histoK1 = 1.0 / vmax;
    histoK2 = 1.0 / (log(vmin * 1.0 / vmax) / (nbin - 1));
  } else {
    if ((nbin > 2) && (vmax > vmin)) {
      histoStep = (vmax - vmin) * 1.0 / (nbin - 2);
      histoK1 = 1.0 / histoStep;
    } else {
      histoStep = 0;
      histoK1 = 0;
    }
  }
  histoCounts.resize(nbin, 0);
}

void CounterStats::getHisto(std::vector<double> &x,
                            std::vector<CounterValue> &count) {
  if (histoNbin) {
    x.resize(histoNbin);
    count.resize(histoNbin);
    for (unsigned int i = 0; i < histoNbin; i++) {
      if (histoLogScale) {
        x[i] = histoVmax * pow(histoStep, histoNbin - 1 - i);
      } else {
        if (i == 0) {
          x[i] = histoVmin;
        } else if (i == histoNbin - 1) {
          x[i] = histoVmax;
        } else {
          x[i] = histoVmin + (i - 1) * histoStep;
        }
      }
      count[i] = histoCounts[i];
    }
  } else {
    x.clear();
    count.clear();
  }
}
