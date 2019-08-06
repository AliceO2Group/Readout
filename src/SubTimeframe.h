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
  uint32_t timeframeId; // id of timeframe
  uint32_t numberOfHBF; // number of HB frames (i.e. following messages)
  uint8_t linkId;       // common link id of all data in this HBframe
};
