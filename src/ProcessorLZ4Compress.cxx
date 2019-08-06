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

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include "lz4.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define ERR_SUCCESS 0
#define ERR_ERROR_UNDEFINED -1
#define ERR_NULL_INPUT -2
#define ERR_MALLOC -3
#define ERR_OUTPUT_BUFFER_TOO_SMALL -4
#define ERR_LZ4_FAILED -5


extern "C"
{
 
  int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output) {

    int err=ERR_ERROR_UNDEFINED; // function error flag, zero for sucess
    
    void *ptrIn=input->getData()->data; // data input
    if (ptrIn==NULL) {return ERR_NULL_INPUT;}
    size_t sizeIn=input->getData()->header.dataSize; // input size (bytes)

    // define the LZ4 format header/trailer
    // https://github.com/lz4/lz4/blob/master/doc/lz4_Frame_format.md

    // maximum size of (uncompressed) block is 4MB max.
    const int blockMaximumSize=4*1024*1024;
    
    // LZ4 frame
    // here we use simplest format with no options:
    // Magic number (4b) FLG (1b) BD (1b) HC (1b) BlockCompressedSize (4b) Data (xxx) EndMark (4b)
    
    const char header[]={
      0x04,0x22,0x4D,0x18,    // Magic Number
      0x60,                   // FLG b01100000 = 0x60 -> Version=01 (bits 6-7), Block Independence flag=1 (bit 5)
      0x70,                   // BD  b01110000 = 0x70 -> Block Maximum Size=111 (bits 4-5-6) 7 -> 4MB
      0x73,                   // HC header checksum (xxh32()>>8) & 0xFF (do not include Magic Number!)
    };
    
    // to get header checksum:
    // #include "xxhash.h"
    // header[6]=(XXH32(header+4,2,0) >> 8) & 0xFF;

    uint32_t blockSize=0; // size of compressed block, 4 bytes little endian after header
    
    const char trailer[]={
      0x00,0x00,0x00,0x00     // EndMark
    };
    
    const int lz4FrameFormatBytes=sizeof(header)+sizeof(blockSize)+sizeof(trailer); // number of bytes needed for LZ4 frame formatting
    
    // size needed for output buffer = maximum size after compression + few bytes for LZ4 file formatting
    int outBufferSize = LZ4_compressBound(sizeIn) + lz4FrameFormatBytes;

    char *ptrOut=nullptr; // output buffer
    if (ptrOut==nullptr) {
      ptrOut=(char *)malloc(outBufferSize);
      if (ptrOut==nullptr) {return ERR_MALLOC;}
    }
    
    // compress
    size_t sizeOut = LZ4_compress_default(input->getData()->data, ptrOut, sizeIn, outBufferSize);
    
    // copy-back result
    if (sizeOut>0) {
      // compression success
      int64_t sizeAvailable=(char *)input->getData()+input->getDataBufferSize()-(char *)ptrIn; // remaining buffer size available after data ptr
      int64_t sizeNeeded=sizeOut+lz4FrameFormatBytes;
      if (sizeNeeded<=sizeAvailable) {
        // we are able to fit result+headers in same buffer

        // update lz4 block size
        blockSize=(uint32_t) sizeOut;
        blockSize &= 0x7FFFFFFF; // highest bit zero when frame is LZ4 compressed
        
        // let's build a formatted output back in provided input buffer
        char *ptrFormatted=(char*)input->getData()->data;
        int ptrSize=0;
        auto push = [&] (const void *source, size_t size) {
          memcpy(&ptrFormatted[ptrSize],source,size);
          ptrSize+=size;
        };

        // append the different pieces
        push(header,sizeof(header));
        push(&blockSize,sizeof(blockSize));
        push(ptrOut,sizeOut);
        push(trailer,sizeof(trailer));

	// artifical sleep, to force un-ordering (used for tests)
	//int ss=rand()*10000.0/RAND_MAX;
	//usleep(ss);

        // looks good, give it back
        output=input;
        output->getData()->header.dataSize=ptrSize;
        err=ERR_SUCCESS;
      } else {
        err=ERR_OUTPUT_BUFFER_TOO_SMALL;
      }
    } else {
      err=ERR_LZ4_FAILED;
    }
    if (ptrOut!=nullptr) {
      free(ptrOut);
    }
    return err;
  }

}  // extern "C"
