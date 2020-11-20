// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef _READOUTUTILS_H
#define _READOUTUTILS_H

#include <Common/Configuration.h>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include "RAWDataHeader.h"

namespace ReadoutUtils
{

// function to convert a string to a 64-bit integer value
// allowing usual "base units" in suffix (k,M,G,T)
// input can be decimal (1.5M is valid, will give 1.5*1024*1024)
long long getNumberOfBytesFromString(const char* inputString);

// function to convert a value in bytes to a prefixed number 3+3 digits
// suffix is the "base unit" to add after calculated prefix, e.g. Byte-> kBytes
std::string NumberOfBytesToString(double value, const char* suffix);

} // namespace ReadoutUtils

// print RDH struct content to stdout
void dumpRDH(o2::Header::RAWDataHeader* rdh);

// parse a string of coma-separated key=value pairs into a map
// e.g. key1=value1, key2=value2, key3=value3 ...
// returns 0 on success, -1 on error
int getKeyValuePairsFromString(const std::string& input, std::map<std::string, std::string>& output);

// parse a string of coma-separated integers into a vector
// returns 0 on success, -1 on error
int getIntegerListFromString(const std::string& input, std::vector<int>& output);

// function to convert a value in bytes to a prefixed number 3+3 digits
// suffix is the "base unit" to add after calculated prefix, e.g. Byte-> kBytes
std::string NumberOfBytesToString(double value, const char* suffix, int base = 1024);

// function to get cumulated user and system CPU time used by current process in seconds.
// returns 0 on success, -1 on error
int getProcessStats(double& uTime, double& sTime);

typedef uint32_t tRunNumber;
typedef uint32_t tTimeframeId;

// end of _READOUTUTILS_H
#endif
