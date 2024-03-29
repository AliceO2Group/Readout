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

#ifndef ALICEO2_HEADER_RAWDATAHEADER_H
#define ALICEO2_HEADER_RAWDATAHEADER_H

// @file   RAWDataHeader.h
// @since  2017-11-22
// @brief  Definition of the RAW Data Header

#include <cstdint>

namespace o2
{
namespace Header
{

/// The definition of the RAW Data Header v2 (RDH) is specified in
/// https://docs.google.com/document/d/1IxCCa1ZRpI3J9j3KCmw2htcOLIRVVdEcO-DDPcLNFM0
/// preliminary description of the fields can be found here
/// https://docs.google.com/document/d/1FLcBrPaF3Bg1Pnm17nwaxNlenKtEk3ocizEAiGP58J8
/// FIXME: replace citation with correct ALICE note reference when published
///
/// Note: the definition requires little endian architecture, for the moment we
/// assume that this is the only type the software has to support (based on
/// experience with previous systems)
///
/// RDH consists of 4 64 bit words
///       63     56      48      40      32      24      16       8       0
///       |---------------|---------------|---------------|---------------|
///
/// 0     | zero  |  size |link id|    FEE id     |  block length | vers  |
///
/// 1     |      heartbeat orbit          |       trigger orbit           |
///
/// 2     | zero  |heartbeatBC|      trigger type             | trigger BC|
///
/// 3     | zero  |      par      | detector field| stop  |  page count   |
///
/// Field description:
/// - version:      the header version number
/// - block length: assumed to be in byte, but discussion not yet finalized
/// - FEE ID:       unique id of the Frontend equipment
/// - Link ID:      id of the link within CRU
/// - header size:  number of 64 bit words
/// - heartbeat and trigger orbit/BC: LHC clock parameters, still under
///                 discussion whether separate fields for HB and trigger
///                 information needed
/// - trigger type: bit fiels fir the trigger type yet to be decided
/// - page count:   incremented if data is bigger than the page size, pages are
///                 incremented starting from 0
/// - stop:         bit 0 of the stop field is set if this is the last page
/// - detector field and par are detector specific fields
struct RAWDataHeaderV2 {
  union {
    // default value
    uint64_t word0 = 0x0004ffffff000002;
    //                   | | |   |   | version 2
    //                   | | |   | block length 0
    //                   | | | invalid FEE id
    //                   | | invalid link id
    //                   | header size 4 x 64 bit
    struct {
      uint64_t version : 8;      /// bit 0 to 8: header version
      uint64_t blockLength : 16; /// bit 9 to 23: block length
      uint64_t feeId : 16;       /// bit 24 to 39: FEE identifier
      uint64_t linkId : 8;       /// bit 40 to 47: link identifier
      uint64_t headerSize : 8;   /// bit 48 to 55: header size
      uint64_t zero0 : 8;        /// bit 56 to 63: zeroed
    };
  };
  union {
    uint64_t word1 = 0x0;
    struct {
      uint32_t triggerOrbit;   /// bit 0 to 31: trigger orbit
      uint32_t heartbeatOrbit; /// bit 32 to 63: trigger orbit
    };
  };
  union {
    uint64_t word2 = 0x0;
    struct {
      uint64_t triggerBC : 12;   /// bit 0 to 11: trigger BC ID
      uint64_t triggerType : 32; /// bit 12 to 43: trigger type
      uint64_t heartbeatBC : 12; /// bit 44 to 55: heartbeat BC ID
      uint64_t zero2 : 8;        /// bit 56 to 63: zeroed
    };
  };
  union {
    uint64_t word3 = 0x0;
    struct {
      uint64_t pageCnt : 16;       /// bit 0 to 15: pages counter
      uint64_t stop : 8;           /// bit 13 to 23: stop code
      uint64_t detectorField : 16; /// bit 24 to 39: detector field
      uint64_t par : 16;           /// bit 40 to 55: par
      uint64_t zero3 : 8;          /// bit 56 to 63: zeroed
    };
  };
};

typedef struct _RAWDataHeaderV3 {
  // 32-bits words

  union {
    uint32_t word3 = 0x00004003;
    //                     | | version 3
    //                     | header size 16x32 bit = 64 bytes
    struct {
      uint32_t version : 8;      /// bit 0 to 7: header version
      uint32_t headerSize : 8;   /// bit 8 to 15: header size
      uint32_t blockLength : 16; /// bit 16 to 31: block length
    };
  };

  union {
    uint32_t word2 = 0x00ffffff;
    struct {
      uint32_t feeId : 16;      /// bit 0 to 15: FEE id
      uint32_t priorityBit : 8; /// bit 16 to 23: priority bit
      uint32_t zero2 : 8;       /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word1 = 0x0;
    struct {
      uint32_t offsetNextPacket : 16; /// bit 0 to 15: offset of next block
      uint32_t memorySize : 16;       /// bit 16 to 31: size of block (in bytes) in memory
    };
  };

  union {
    uint32_t word0 = 0x0;
    struct {
      uint32_t linkId : 8; /// bit 0 to 7: linkId
      uint32_t zero0 : 24; /// bit 8 to 31: reserved
    };
  };

  union {
    uint32_t word7 = 0xffffffff;
    struct {
      uint32_t triggerOrbit; /// bit 0 to 31: TRG orbit
    };
  };

  union {
    uint32_t word6 = 0xffffffff;
    struct {
      uint32_t heartbeatOrbit; /// bit 0 to 31: HB orbit
    };
  };

  union {
    uint32_t word5 = 0x0;
    struct {
      uint32_t zero5; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word4 = 0x0;
    struct {
      uint32_t zero4; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word11 = 0x0;
    struct {
      uint32_t triggerBC : 12;   /// bit 0 to 11: TRG BC ID
      uint32_t zero11_0 : 4;     /// bit 12 to 15: reserved
      uint32_t heartbeatBC : 12; /// bit 16 to 27: HB BC ID
      uint32_t zero11_1 : 4;     /// bit 28 to 31: reserved
    };
  };

  union {
    uint32_t word10 = 0x0;
    struct {
      uint32_t triggerType : 32; /// bit 0 to 31: trigger types
    };
  };

  union {
    uint32_t word9 = 0x0;
    struct {
      uint32_t zero9; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word8 = 0x0;
    struct {
      uint32_t zero8; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word15 = 0x0;
    struct {
      uint32_t detectorField : 16; /// bit 0 to 15: detector field
      uint32_t par : 16;           /// bit 16 to 31: PAR
    };
  };

  union {
    uint32_t word14 = 0x0;
    struct {
      uint32_t stopBit : 8;       /// bit 0 to 7: stop bit
      uint32_t pagesCounter : 16; /// bit 8 to 23: pages counter
      uint32_t zero14 : 8;        /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word13 = 0x0;
    struct {
      uint32_t zero13; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word12 = 0x0;
    struct {
      uint32_t zero12; /// bit 0 to 31: reserved
    };
  };

} RAWDataHeaderV3;

typedef struct _RAWDataHeaderV4 {
  // 32-bits words

  union {
    uint32_t word3 = 0x00004004;
    //                     | | version 4
    //                     | header size 16x32 bit = 64 bytes
    struct {
      uint32_t version : 8;      /// bit 0 to 7: header version
      uint32_t headerSize : 8;   /// bit 8 to 15: header size
      uint32_t blockLength : 16; /// bit 16 to 31: block length
    };
  };

  union {
    uint32_t word2 = 0x00ffffff;
    struct {
      uint32_t feeId : 16;      /// bit 0 to 15: FEE id
      uint32_t priorityBit : 8; /// bit 16 to 23: priority bit
      uint32_t zero2 : 8;       /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word1 = 0x0;
    struct {
      uint32_t offsetNextPacket : 16; /// bit 0 to 15: offset of next block
      uint32_t memorySize : 16;       /// bit 16 to 31: size of block (in bytes) in memory
    };
  };

  union {
    uint32_t word0 = 0xffffffff;
    struct {
      uint32_t linkId : 8;        /// bit 0 to 7: link id (GBT channel number)
      uint32_t packetCounter : 8; /// bit 8 to 15: packet counter (increased at every packet received in the link)
      uint32_t cruId : 12;        /// bit 16 to 27: CRU id
      uint32_t dpwId : 4;         /// bit 28 to 31: data path wrapper id, used to identify one of the 2 CRU End Points
    };
  };

  union {
    uint32_t word7 = 0xffffffff;
    struct {
      uint32_t triggerOrbit; /// bit 0 to 31: TRG orbit
    };
  };

  union {
    uint32_t word6 = 0xffffffff;
    struct {
      uint32_t heartbeatOrbit; /// bit 0 to 31: HB orbit
    };
  };

  union {
    uint32_t word5 = 0x0;
    struct {
      uint32_t zero5; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word4 = 0x0;
    struct {
      uint32_t zero4; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word11 = 0x0;
    struct {
      uint32_t triggerBC : 12;   /// bit 0 to 11: TRG BC ID
      uint32_t zero11_0 : 4;     /// bit 12 to 15: reserved
      uint32_t heartbeatBC : 12; /// bit 16 to 27: HB BC ID
      uint32_t zero11_1 : 4;     /// bit 28 to 31: reserved
    };
  };

  union {
    uint32_t word10 = 0x0;
    struct {
      uint32_t triggerType : 32; /// bit 0 to 31: trigger types
    };
  };

  union {
    uint32_t word9 = 0x0;
    struct {
      uint32_t zero9; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word8 = 0x0;
    struct {
      uint32_t zero8; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word15 = 0x0;
    struct {
      uint32_t detectorField : 16; /// bit 0 to 15: detector field
      uint32_t par : 16;           /// bit 16 to 31: PAR
    };
  };

  union {
    uint32_t word14 = 0x0;
    struct {
      uint32_t stopBit : 8;       /// bit 0 to 7: stop bit
      uint32_t pagesCounter : 16; /// bit 8 to 23: pages counter
      uint32_t zero14 : 8;        /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word13 = 0x0;
    struct {
      uint32_t zero13; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word12 = 0x0;
    struct {
      uint32_t zero12; /// bit 0 to 31: reserved
    };
  };

} RAWDataHeaderV4;

typedef struct _RAWDataHeaderV6 {
  // 32-bits words

  union {
    uint32_t word0 = 0xffff4006;
    //                     | | version 6
    //                     | header size 16x32 bit = 64 bytes
    struct {
      uint32_t version : 8;    /// bit 0 to 7: header version
      uint32_t headerSize : 8; /// bit 8 to 15: header size
      uint32_t feeId : 16;     /// bit 16 to 31: FEE id
    };
  };

  union {
    uint32_t word1 = 0x0000ffff;
    struct {
      uint32_t priorityBit : 8; /// bit 0 to 7: priority bit
      uint32_t systemId : 8;    /// bit 8 to 15: system id
      uint32_t zero1 : 16;      /// bit 16 to 31: reserved
    };
  };

  union {
    uint32_t word2 = 0x0;
    struct {
      uint32_t offsetNextPacket : 16; /// bit 0 to 15: offset of next block
      uint32_t memorySize : 16;       /// bit 16 to 31: size of block (in bytes) in memory
    };
  };

  union {
    uint32_t word3 = 0xffffffff;
    struct {
      uint32_t linkId : 8;        /// bit 0 to 7: link id (GBT channel number)
      uint32_t packetCounter : 8; /// bit 8 to 15: packet counter (increased at every packet received in the link)
      uint32_t cruId : 12;        /// bit 16 to 27: CRU id
      uint32_t dpwId : 4;         /// bit 28 to 31: data path wrapper id, used to identify one of the 2 CRU End Points
    };
  };

  union {
    uint32_t word4 = 0x00000fff;
    struct {
      uint32_t triggerBC : 12; /// bit 0 to 11: TRG BC ID
      uint32_t zero4 : 20;     /// bit 12 to 31: reserved
    };
  };

  union {
    uint32_t word5 = 0xffffffff;
    struct {
      // there's a single orbit counter now
      union {
        uint32_t triggerOrbit;   /// bit 0 to 31: orbit
        uint32_t heartbeatOrbit; /// bit 0 to 31: orbit
      };
    };
  };

  union {
    uint32_t word6 = 0x0;
    struct {
      uint32_t zero6; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word7 = 0x0;
    struct {
      uint32_t zero7; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word8 = 0xffffffff;
    struct {
      uint32_t triggerType : 32; /// bit 0 to 31: trigger types
    };
  };

  union {
    uint32_t word9 = 0x00ffffff;
    struct {
      uint32_t pagesCounter : 16; /// bit 0 to 15: pages counter
      uint32_t stopBit : 8;       /// bit 16 to 23: stop bit
      uint32_t zero9 : 8;         /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word10 = 0x0;
    struct {
      uint32_t zero10; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word11 = 0x0;
    struct {
      uint32_t zero11; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word12 = 0xffffffff;
    struct {
      uint32_t detectorField; /// bit 0 to 31: detector field
    };
  };

  union {
    uint32_t word13 = 0x0000ffff;
    struct {
      uint16_t par;    /// bit 0 to 15: PAR
      uint16_t zero13; /// bit 16 to 31: reserved
    };
  };

  union {
    uint32_t word14 = 0x0;
    struct {
      uint32_t zero14; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word15 = 0x0;
    struct {
      uint32_t zero15; /// bit 0 to 31: reserved
    };
  };

} RAWDataHeaderV6;

// as defined in https://gitlab.cern.ch/AliceO2Group/wp6-doc/-/blob/master/rdh/RDHv7.md
typedef struct _RAWDataHeaderV7 {
  // 32-bits words

  union {
    uint32_t word0 = 0xffff4007;
    //                     | | version 7
    //                     | header size 16x32 bit = 64 bytes
    struct {
      uint32_t version : 8;    /// bit 0 to 7: header version
      uint32_t headerSize : 8; /// bit 8 to 15: header size
      uint32_t feeId : 16;     /// bit 16 to 31: FEE id
    };
  };

  union {
    uint32_t word1 = 0x0000ffff;
    struct {
      uint32_t priorityBit : 8; /// bit 0 to 7: priority bit
      uint32_t systemId : 8;    /// bit 8 to 15: system id
      uint32_t zero1 : 16;      /// bit 16 to 31: reserved
    };
  };

  union {
    uint32_t word2 = 0x0;
    struct {
      uint32_t offsetNextPacket : 16; /// bit 0 to 15: offset of next block
      uint32_t memorySize : 16;       /// bit 16 to 31: size of block (in bytes) in memory
    };
  };

  union {
    uint32_t word3 = 0xffffffff;
    struct {
      uint32_t linkId : 8;        /// bit 0 to 7: link id (GBT channel number)
      uint32_t packetCounter : 8; /// bit 8 to 15: packet counter (increased at every packet received in the link)
      uint32_t cruId : 12;        /// bit 16 to 27: CRU id
      uint32_t dpwId : 4;         /// bit 28 to 31: data path wrapper id, used to identify one of the 2 CRU End Points
    };
  };

  union {
    uint32_t word4 = 0x00000fff;
    struct {
      uint32_t triggerBC : 12; /// bit 0 to 11: TRG BC ID
      uint32_t zero4 : 20;     /// bit 12 to 31: reserved
    };
  };

  union {
    uint32_t word5 = 0xffffffff;
    struct {
      // there's a single orbit counter now
      union {
        uint32_t triggerOrbit;   /// bit 0 to 31: orbit
        uint32_t heartbeatOrbit; /// bit 0 to 31: orbit
      };
    };
  };

  union {
    uint32_t word6 = 0x0;
    struct {
      uint32_t dataFormat : 8; /// bit 0 to 7 : data format
      uint32_t zero6 : 24;     /// bit 8 to 31: reserved
    };
  };

  union {
    uint32_t word7 = 0x0;
    struct {
      uint32_t zero7; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word8 = 0xffffffff;
    struct {
      uint32_t triggerType : 32; /// bit 0 to 31: trigger types
    };
  };

  union {
    uint32_t word9 = 0x00ffffff;
    struct {
      uint32_t pagesCounter : 16; /// bit 0 to 15: pages counter
      uint32_t stopBit : 8;       /// bit 16 to 23: stop bit
      uint32_t zero9 : 8;         /// bit 24 to 31: reserved
    };
  };

  union {
    uint32_t word10 = 0x0;
    struct {
      uint32_t zero10; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word11 = 0x0;
    struct {
      uint32_t zero11; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word12 = 0xffffffff;
    struct {
      uint32_t detectorField; /// bit 0 to 31: detector field
    };
  };

  union {
    uint32_t word13 = 0x0000ffff;
    struct {
      uint16_t par;    /// bit 0 to 15: PAR
      uint16_t zero13; /// bit 16 to 31: reserved
    };
  };

  union {
    uint32_t word14 = 0x0;
    struct {
      uint32_t zero14; /// bit 0 to 31: reserved
    };
  };

  union {
    uint32_t word15 = 0x0;
    struct {
      uint32_t zero15; /// bit 0 to 31: reserved
    };
  };

} RAWDataHeaderV7;

// definition of triggerType RDH field
typedef struct _RDHTriggerType {
  union {
    uint32_t word0 = 0;
    struct {
      uint32_t ORBIT : 1;
      uint32_t HB : 1;
      uint32_t HBr : 1;
      uint32_t HC : 1;
      uint32_t PhT : 1;
      uint32_t PP : 1;
      uint32_t Cal : 1;
      uint32_t SOT : 1;
      uint32_t EOT : 1;
      uint32_t SOC : 1;
      uint32_t EOC : 1;
      uint32_t TF : 1;
      uint32_t FErst : 1;
      uint32_t RT : 1;
      uint32_t RS : 1;
      uint32_t spare : 12;
      uint32_t LHCgap1 : 1;
      uint32_t LHCgap2 : 1;
      uint32_t TPCsync : 1;
      uint32_t TPCrst : 1;
      uint32_t TOF : 1;
    };
  };
} RDHTriggerType;

using RAWDataHeader = RAWDataHeaderV6;

// expecting 16*32bits = 64 bytes
static_assert(sizeof(RAWDataHeader) == 64);

} // namespace Header
} // namespace o2

#endif // ALICEO2_HEADER_RAWDATAHEADER_H

