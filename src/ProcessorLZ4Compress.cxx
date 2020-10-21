// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/*
  This processor compresses data with LZ4 algorithm https://lz4.github.io/lz4/
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "lz4.h"

#define ERR_SUCCESS 0
#define ERR_ERROR_UNDEFINED -1
#define ERR_NULL_INPUT -2
#define ERR_MALLOC -3
#define ERR_OUTPUT_BUFFER_TOO_SMALL -4
#define ERR_LZ4_FAILED -5

extern "C" {

int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output) {

  output = nullptr;

  int err = ERR_ERROR_UNDEFINED; // function error flag, zero for sucess

  if (input->getData()->data == NULL) {
    return ERR_NULL_INPUT;
  }
  size_t sizeIn = input->getData()->header.dataSize; // input size (bytes)

  // define the LZ4 format header/trailer
  // https://github.com/lz4/lz4/blob/master/doc/lz4_Frame_format.md

  // maximum size of (uncompressed) block is 4MB max.
  // const int blockMaximumSize = 4 * 1024 * 1024;

  // LZ4 frame
  // here we use simplest format with no options:
  // Magic number (4b) FLG (1b) BD (1b) HC (1b) BlockCompressedSize (4b) Data
  // (xxx) EndMark (4b)

  const char header[] = {
      0x04, 0x22, 0x4D, 0x18, // Magic Number
      0x60,                   // FLG b01100000 = 0x60 -> Version=01 (bits 6-7), Block Independence
                              // flag=1 (bit 5)
      0x70,                   // BD  b01110000 = 0x70 -> Block Maximum Size=111 (bits 4-5-6) 7 ->
                              // 4MB
      0x73,                   // HC header checksum (xxh32()>>8) & 0xFF (do not include Magic
                              // Number!)
  };

  // to get header checksum:
  // #include "xxhash.h"
  // header[6]=(XXH32(header+4,2,0) >> 8) & 0xFF;

  uint32_t blockSize = 0; // size of compressed block, 4 bytes little endian after header

  const char trailer[] = {
      0x00, 0x00, 0x00, 0x00 // EndMark
  };

  const int lz4FrameFormatBytes = sizeof(header) + sizeof(blockSize) + sizeof(trailer); // number of bytes needed for LZ4 frame formatting

  const bool reuseInputBufferForOutput = 0; // flag to select copy method

  // size needed for final output buffer
  // = maximum size after compression + few bytes for LZ4 file formatting
  int64_t maxCompressedSize = LZ4_compressBound(sizeIn);
  int64_t maxFormattedSize = maxCompressedSize + lz4FrameFormatBytes;

  char *ptrCompressed = nullptr; // buffer for compressed data
  char *ptrFormatted = nullptr;  // buffer for formatted compressed data with header/trailer

  size_t ptrCompressedSize = 0; // allocated buffer size
  size_t ptrFormattedSize = 0;  // allocated buffer size

  if (!reuseInputBufferForOutput) {
    // create a new data block + container from malloc()
    // (not using a predefined memory bank,
    // which are not readily available from consumers)
    size_t pageSize = sizeof(DataBlock) + maxFormattedSize;
    void *newPage = malloc(pageSize);
    if (newPage == nullptr) {
      return ERR_MALLOC;
    }

    // fill header at beginning of page
    // assuming payload is contiguous after header
    DataBlock *b = (DataBlock *)newPage;
    b->header = input->getData()->header;
    b->data = &(((char *)b)[sizeof(DataBlock)]);

    auto releaseCallback = [newPage](void) -> void {
      free(newPage);
      return;
    };

    // create a container and associate data page and release callback
    std::shared_ptr<DataBlockContainer> bc = std::make_shared<DataBlockContainer>(releaseCallback, (DataBlock *)newPage, pageSize);
    if (bc == nullptr) {
      releaseCallback();
      return ERR_MALLOC;
    }

    ptrFormatted = (char *)b->data;
    ptrCompressed = &(ptrFormatted[sizeof(header) + sizeof(blockSize)]); // leave suitable space in front
    ptrCompressedSize = maxCompressedSize;
    ptrFormattedSize = maxFormattedSize;
    output = bc; // new data block as output

  } else {

    // create a temporary buffer for compression
    // will be copied back to existing input DataBlock
    ptrCompressed = (char *)malloc(maxCompressedSize);
    ptrCompressedSize = maxCompressedSize;
    ptrFormatted = (char *)input->getData()->data;
    ptrFormattedSize = (char *)input->getData() + input->getDataBufferSize() - (char *)input->getData()->data; // remaining buffer size available after data ptr
    output = input;                                                                                            // reuse existing data block as output (might be wrong: other consumers will see modified data)
  }

  if ((ptrCompressed == nullptr) || (ptrFormatted == nullptr)) {
    return ERR_MALLOC;
  }

  // compress
  size_t sizeOut = LZ4_compress_default(input->getData()->data, ptrCompressed, sizeIn, ptrCompressedSize);

  // format output
  if (sizeOut > 0) {
    // compression success

    if (sizeOut + lz4FrameFormatBytes > ptrFormattedSize) {
      err = ERR_OUTPUT_BUFFER_TOO_SMALL;
    } else {
      // we are able to fit result+headers in same buffer

      // update lz4 block size
      blockSize = (uint32_t)sizeOut;
      blockSize &= 0x7FFFFFFF; // highest bit zero when frame is LZ4 compressed

      // let's build a formatted output back in provided input buffer
      int ptrSize = 0;
      auto push = [&ptrSize, &ptrFormatted](const void *source, size_t size) {
        void *dest = &ptrFormatted[ptrSize];
        // copy only if needed
        if (dest != source) {
          memcpy(dest, source, size);
        }
        ptrSize += size;
      };

      // append the different pieces
      push(header, sizeof(header));
      push(&blockSize, sizeof(blockSize));
      push(ptrCompressed, sizeOut);
      push(trailer, sizeof(trailer));

      // artifical sleep, to force un-ordering (used for tests)
      // int ss=rand()*10000.0/RAND_MAX;
      // usleep(ss);

      // looks good, give it back
      output->getData()->header.dataSize = ptrSize;
      err = ERR_SUCCESS;
    }
  } else {
    err = ERR_LZ4_FAILED;
  }
  if (reuseInputBufferForOutput) {
    if (ptrCompressed != nullptr) {
      free(ptrCompressed);
    }
  }
  if (err != ERR_SUCCESS) {
    output = nullptr;
  }
  return err;
}

} // extern "C"
