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

// Utilities to handle RDH content from CRU data

#ifndef RDHUTILS_H
#define RDHUTILS_H

#include <string>

#include "RAWDataHeader.h"

// Some constants
const unsigned int RdhMaxLinkId = 31; // maximum ID of a linkId in RDH

// Utility class to access RDH fields and check them
class RdhHandle
{
 public:
  // create a handle to RDH structure pointed by argument
  RdhHandle(void* data);

  // destructor
  ~RdhHandle();

  // check RDH content
  // returns 0 on success, number of errors found otherwise
  // Error message sets accordingly
  int validateRdh(std::string& err);

  // print RDH content
  // offset is a value to be displayed as address. if -1, memory address is used.
  // singleLine: when set, RDH content printed in single line with top header printed once
  void dumpRdh(long offset = -1, bool singleLine = false);

  // access RDH fields
  // functions defined inline here
  inline uint8_t getHeaderVersion() { return rdhPtr->version; }
  inline uint8_t getSystemId() { return (uint8_t)rdhPtr->systemId; }
  inline uint16_t getFeeId() { return (uint16_t)rdhPtr->feeId; }
  inline uint8_t getLinkId() { return (uint8_t)rdhPtr->linkId; }
  inline uint8_t getPacketCounter() { return (uint8_t)rdhPtr->packetCounter; }
  inline uint8_t getHeaderSize() { return rdhPtr->headerSize; }
  inline uint32_t getHbOrbit() { return (uint32_t)rdhPtr->heartbeatOrbit; }
  inline void incrementHbOrbit(uint32_t offset) { rdhPtr->heartbeatOrbit += offset; }
  inline uint16_t getMemorySize() { return (uint16_t)rdhPtr->memorySize; }
  inline uint16_t getOffsetNextPacket() { return (uint16_t)rdhPtr->offsetNextPacket; }
  inline bool getStopBit() { return (bool)rdhPtr->stopBit; }
  inline uint16_t getPagesCounter() { return (uint16_t)rdhPtr->pagesCounter; }
  inline uint32_t getTriggerOrbit() { return (uint32_t)rdhPtr->triggerOrbit; }
  inline uint32_t getTriggerBC() { return (uint32_t)rdhPtr->triggerBC; }
  inline uint32_t getTriggerType() { return (uint32_t)rdhPtr->triggerType; }
  inline o2::Header::RDHTriggerType getTriggerTypeStruct() { o2::Header::RDHTriggerType tt; tt.word0 = rdhPtr->triggerType; return tt; }
  inline uint16_t getCruId() { return (uint16_t)rdhPtr->cruId; }
  inline uint8_t getEndPointId() { return (uint8_t)rdhPtr->dpwId; }
  inline uint32_t getDetectorField() { return (uint32_t)rdhPtr->detectorField; }

  uint64_t computeTimeframeId(const uint32_t firstTimeframeHbOrbitBegin, const uint32_t timeframePeriodOrbits) {
    tfId = 1 + (getHbOrbit() - firstTimeframeHbOrbitBegin) / timeframePeriodOrbits;
    return tfId;
  }

  // returns an hexadecimal string from RDH, in 32-bit words
  std::string toHexaString();

 private:
  o2::Header::RAWDataHeader* rdhPtr; // pointer to RDH in memory
  uint64_t tfId = 0; // computed timeframeId
};

// Utility class to access/parse/check the content of a contiguous memory block consisting of RDH+data
class RdhBlockHandle
{
 public:
  // create a handle to the block, providing pointer and size
  RdhBlockHandle(void* blockPtr, size_t size);

  // destructor
  ~RdhBlockHandle();

  // print summary
  // return 0 on success, an error code if the block is invalid
  int printSummary();

 private:
  void* blockPtr;   // pointer to beginning of memory block
  size_t blockSize; // size of memory block
};

#endif

