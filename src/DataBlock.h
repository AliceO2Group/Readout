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

/// \file DataBlock.h
///
/// This defines the POD structures used in readout to organize data in memory.
/// The base component is called a DataBlock, and is a pair: header + payload data
/// (usually but not necessarily contiguous).
/// The header contains tags and identifiers associated with the data.
/// The payload is a blob of any format.
/// When DataBlocks are contiguous:
///   headerPtr + headerSize = payloadPtr
///   headerPtr + headerSize + dataSize = nextHeaderPtr
///
/// \author Sylvain Chapeland, CERN

#ifndef READOUT_DATABLOCK
#define READOUT_DATABLOCK

#include <stdint.h>
#include <type_traits>

// Definition of a unique identifier for blocks
typedef uint64_t DataBlockId;

// Definition of default values
const uint64_t undefinedBlockId = 0;          ///< default value, when blockId undefined
const uint64_t undefinedTimeframeId = 0;      ///< default value, when timeframeId undefined
const uint32_t undefinedRunNumber = 0;        ///< default value, when runNumber undefined
const uint8_t undefinedSystemId = 0xFF;       ///< default value, when linkId undefined
const uint16_t undefinedFeeId = 0xFFFF;       ///< default value, when linkId undefined
const uint16_t undefinedEquipmentId = 0xFFFF; ///< default value, when equipmentId undefined
const uint8_t undefinedLinkId = 0xFF;         ///< default value, when linkId undefined
const uint32_t undefinedOrbit = 0;            ///< default value, when orbit undefined

const uint32_t DataBlockHeaderUserSpace = 124; ///< size of spare area for user data

// Header
struct DataBlockHeader {

  uint32_t headerVersion; ///< id to identify structure
  uint32_t headerSize;    ///< header size in bytes
  uint32_t dataSize;      ///< size of payload following or associated with this structure

  DataBlockId blockId;          ///< id of the block (strictly monotonic increasing sequence)
  DataBlockId pipelineId;       ///< id used to sort data in/out in parallel pipelines
  uint64_t timeframeId;         ///< id of timeframe
  uint64_t runNumber;           ///< the current run number
  uint8_t systemId;             ///< from RDH
  uint16_t feeId;               ///< from RDH
  uint16_t equipmentId;         ///< id of equipment generating the data
  uint8_t linkId;               ///< from RDH
  uint32_t timeframeOrbitFirst; ///< from timeframe
  uint32_t timeframeOrbitLast;  ///< from timeframe
  uint8_t flagEndOfTimeframe;   ///< flag to signal this is the last TF block
  uint8_t isRdhFormat;          ///< flag set when payload is RDH-formatted
  uint32_t orbitFirstInBlock;   ///< the first orbit in this block
  uint32_t orbitOffset;         ///< set when RDH orbits should be added given offset to match TFid
  uint32_t memorySize;          ///< size in memory of current block

  uint8_t userSpace[DataBlockHeaderUserSpace]; ///< spare area for user data
};

// Version of this header
// with DB marker for DataBlock start, 1st byte in header little-endian
const uint32_t DataBlockVersion = 0x0005DBDB;

// DataBlockHeader instance with all default fields
const DataBlockHeader defaultDataBlockHeader = { .headerVersion = DataBlockVersion, .headerSize = sizeof(DataBlockHeader), .dataSize = 0, .blockId = undefinedBlockId, .pipelineId = undefinedBlockId, .timeframeId = undefinedTimeframeId, .runNumber = undefinedRunNumber, .systemId = undefinedSystemId, .feeId = undefinedFeeId, .equipmentId = undefinedEquipmentId, .linkId = undefinedLinkId, .timeframeOrbitFirst = undefinedOrbit, .timeframeOrbitLast = undefinedOrbit, .flagEndOfTimeframe = 0, .isRdhFormat = 1, .orbitFirstInBlock = undefinedOrbit, .orbitOffset = undefinedOrbit, .memorySize = 0, .userSpace = { 0 } };

// DataBlock
// Pair of header + payload data
typedef struct {
  DataBlockHeader header; ///< Data header
  char* data;             ///< Pointer to data. May or may not immediately follow this struct.
} DataBlock;

// DataBlock instance with all default fields
const DataBlock defaultDataBlock = { .header = defaultDataBlockHeader, .data = nullptr };

// compile-time checks
static_assert(std::is_trivially_copyable<DataBlockHeader>::value, "DataBlockHeader is not a POD");
static_assert(std::is_trivially_copyable<DataBlock>::value, "DataBlock is not a POD");

#endif /* READOUT_DATABLOCK */

