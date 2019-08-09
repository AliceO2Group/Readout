// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "RdhUtils.h"
RdhHandle::RdhHandle(void *data) { rdhPtr = (o2::Header::RAWDataHeader *)data; }

RdhHandle::~RdhHandle() {}

void RdhHandle::dumpRdh(long offset) {
  if (offset == -1) {
    printf("RDH @ 0x%p\n", (void *)rdhPtr);
  } else {
    printf("RDH @ 0x%08lX\n", offset);
  }
  printf("Version       = 0x%02X\n", (int)getHeaderVersion());
  printf("Header size   = %d\n", (int)getHeaderSize());
  printf("Block length (link) = %d bytes\n", (int)getBlockLength());
  printf("Block length (memory) = %d bytes\n", (int)getMemorySize());
  printf("FEE Id        = %d\n", (int)getFeeId());
  printf("Link Id       = %d\n", (int)getLinkId());
  printf("Next block    = %d\n", (int)getOffsetNextPacket());
  printf("Trigger Orbit / BC = %08X : %03X\n", getTriggerOrbit(),
         getTriggerBC());
  // printf("%04X %04X %04X
  // %04X\n",rdhPtr->word3,rdhPtr->word2,rdhPtr->word1,rdhPtr->word0);
}

int RdhHandle::validateRdh(std::string &err) {
  int retCode = 0;
  // expecting RDH v3 or v4
  if ((getHeaderVersion() != 3) && (getHeaderVersion() != 4)) {
    err += "Wrong header version\n";
    retCode++;
  }
  // expecting 16*32bits=64 bytes for header
  if (getHeaderSize() != 64) {
    err += "Wrong header size\n";
    retCode++;
  }
  // expecting linkId 0-31
  if (getLinkId() > RdhMaxLinkId) {
    err += "Wrong link ID\n";
    retCode++;
  }

  /*
  // expecting block length <= 8kB
  if (getBlockLength()>8*1024) {
    err+="Wrong block length " + std::to_string(getBlockLength()) +"\n";
    retCode++;
  }
  */

  // check FEE Id ?
  return retCode;
}

RdhBlockHandle::RdhBlockHandle(void *ptr, size_t size)
    : blockPtr(ptr), blockSize(size) {}

RdhBlockHandle::~RdhBlockHandle() {}

int RdhBlockHandle::printSummary() {
  printf("\n\n************************\n");
  printf("Start of page %p (%zu bytes)\n\n", blockPtr, blockSize);

  // intialize start of block
  uint8_t *ptr = (uint8_t *)(blockPtr);
  size_t bytesLeft = blockSize;

  int rdhcount = 0;

  for (;;) {

    // check enough space for RDH
    if (bytesLeft < sizeof(o2::Header::RAWDataHeader)) {
      printf("page too small, %zu bytes left! need at least %d bytes for RDH\n",
             bytesLeft, (int)sizeof(o2::Header::RAWDataHeader));
      return -1;
    }

    rdhcount++;
    int offset = ptr - (uint8_t *)blockPtr;
    printf("*** RDH #%d @ 0x%04X = %d\n", rdhcount, offset, offset);

    // print raw bytes
    // printf("Raw bytes dump (32-bit words):\n");
    for (int i = 0; i < sizeof(o2::Header::RAWDataHeader) / sizeof(int32_t);
         i++) {
      if (i % 8 == 0) {
        printf("\n");
      }
      printf("%08X ", (int)(((uint32_t *)ptr)[i]));
    }
    printf("\n\n");

    RdhHandle rdh(ptr);
    rdh.dumpRdh();
    printf("\n");

    int next = rdh.getOffsetNextPacket(); // next RDH
    if (next == 0) {
      break;
    }

    // check enough space to go to next offset
    if (bytesLeft < next) {
      printf("page too small, %zu bytes left! need at least %d bytes for next "
             "offset\n",
             bytesLeft, (int)rdh.getOffsetNextPacket());
      return -1;
    }

    bytesLeft -= next;
    ptr += next;
    if (bytesLeft == 0) {
      break;
    }
  }

  printf("End of page %p (%zu bytes)", blockPtr, blockSize);
  printf("\n************************\n\n");

  return 0;
}
