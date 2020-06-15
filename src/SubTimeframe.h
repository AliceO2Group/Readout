// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

// definition of a header message for a subtimeframe
// subtimeframe made of 1 message with this header
// followed by 1 message for each heartbeat-frame
// All data come from the same data source (same linkId - but possibly different
// FEE ids)

struct SubTimeframe {
  uint64_t timeframeId = 0; // id of timeframe
  uint8_t systemId = 0xFF;
  uint16_t feeId = 0xFFFF;
  uint16_t equipmentId = 0xFF;
  uint8_t linkId = 0xFF;
  uint32_t timeframeOrbitFirst = 0;
  uint32_t timeframeOrbitLast = 0;
  union {
    uint8_t flags = 0;
    struct {
      uint8_t lastTFMessage : 1; // bit 0
      uint8_t flagsUnused : 7;   // bit 1-7: unused
    };
  };
};
