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

// definition of a header message for a subtimeframe
// subtimeframe made of 1 message with this header
// followed by 1 message for each heartbeat-frame
// All data come from the same data source (same linkId - but possibly different FEE ids)

struct SubTimeframe {
  uint8_t version = 2;      // version of this structure
  uint32_t timeframeId = 0; // id of timeframe
  uint32_t runNumber = 0;
  uint8_t systemId = 0xFF;
  uint16_t feeId = 0xFFFF;
  uint16_t equipmentId = 0xFFFF;
  uint8_t linkId = 0xFF;
  uint32_t timeframeOrbitFirst = 0;
  uint32_t timeframeOrbitLast = 0;
  union {
    uint8_t flags = 0;
    struct {
      uint8_t lastTFMessage : 1; // bit 0
      uint8_t isRdhFormat : 1;   // bit 1
      uint8_t flagsUnused : 6;   // bit 2-7: unused
    };
  };
};

