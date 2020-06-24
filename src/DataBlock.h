/// \file DataBlock.h
/// \brief C interface for a general data format.
///
/// Data are organized in a succession of continuous HEADER + PAYLOAD in memory.
/// Each PAYLOAD is also a succession of headers and payloads, up to a certain point (i.e. when payload is not a concatenation of things).
///
/// A base header is defined, and common to all data block types.
/// Each data block type may have a specialized header based on DataBlockHeaderBase.
/// It should be a concatenation of DataBlockHeaderBase and of additionnal fields.
///
/// \author Sylvain Chapeland, CERN

#ifndef DATAFORMAT_DATABLOCK
#define DATAFORMAT_DATABLOCK

#include <stdint.h>

/// Definition of data block types and their associated header.
typedef enum {
  H_BASE = 0xBB, ///< base header type
  H_EOM = 0xFF,  ///< End Of Message header
} DataBlockType;

/// Definition of a unique identifier for blocks
typedef uint64_t DataBlockId;

/// Definition of default values
const uint64_t undefinedBlockId = 0;        ///< default value, when blockId undefined
const uint64_t undefinedTimeframeId = 0;    ///< default value, when timeframeId undefined
const uint8_t undefinedSystemId = 0xFF;      ///< default value, when linkId undefined
const uint16_t undefinedFeeId = 0xFFFF;      ///< default value, when linkId undefined
const uint16_t undefinedEquipmentId = 0xFFFF; ///< default value, when equipmentId undefined
const uint8_t undefinedLinkId = 0xFF;      ///< default value, when linkId undefined
const uint32_t undefinedOrbit = 0;          ///< default value, when orbit undefined


/// Data structure is based on standard data types with specified width.
/// headerPtr + headerSize = payloadPtr
/// headerPtr + headerSize + dataSize = nextHeaderPtr (if not toplevel block header)
typedef struct {
  uint32_t blockType;     ///< ID to identify structure type
  uint32_t headerSize;    ///< header size in bytes
  uint32_t dataSize;      ///< data size following this structure (until next header, if this is not a toplevel block header)
  union {
    uint32_t reserved[61];
    struct {
      DataBlockId blockId = undefinedBlockId;       ///< id of the block (strictly monotonic increasing sequence)    
      DataBlockId pipelineId = undefinedBlockId;    ///< id used to sort data in/out in parallel pipelines
            
      uint64_t timeframeId = undefinedTimeframeId;  ///< id of timeframe
      uint8_t systemId = undefinedSystemId;        /// from RDH
      uint16_t feeId = undefinedFeeId;             /// from RDH
      uint16_t equipmentId = undefinedEquipmentId; /// id of equipment generating the data
      uint8_t linkId = undefinedLinkId;            /// from RDH
      uint32_t timeframeOrbitFirst = undefinedOrbit; /// from timeframe
      uint32_t timeframeOrbitLast = undefinedOrbit;  /// from timeframe
      bool flagEndOfTimeframe = 0 ; ///< flag to signal this is the last TF block
      bool isRdhFormat = 1; ///< flag set when data 
    };
  };

} DataBlockHeaderBase;

/// Add extra types below, e.g.
///
/// typedef struct {
///   DataBlockHeaderBase header;   ///< Base common data header
///   int numberOfSubTimeframes;    ///< number of subtimeframes in payload
/// } DataBlockHeaderTimeframe;

typedef struct
{
  DataBlockHeaderBase header; ///< Base common data header
  char* data;                 ///< Pointer to data. May or may not immediately follow this variable.
} DataBlock;

#endif /* DATAFORMAT_DATABLOCK */
