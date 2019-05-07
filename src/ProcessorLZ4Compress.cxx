/*
  This processor compresses data with LZ4 algorithm https://lz4.github.io/lz4/ 
*/

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include "lz4.h"
#include <string.h>
#include <stdint.h>


extern "C"
{
 
  int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output) {

    int err=-1; // function error flag, zero for sucess
    
    void *ptrIn=input->getData()->data; // data input
    if (ptrIn==NULL) {return -1;}
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
    
    // size of buffer to allocate = maximum size after compression + few bytes for LZ4 file formatting
    int max = LZ4_compressBound(sizeIn) + lz4FrameFormatBytes;

    char *ptrOut=nullptr; // output buffer
    if (ptrOut==nullptr) {
      ptrOut=(char *)malloc(max);
      if (ptrOut==nullptr) {return -1;}
    }
    
    // compress
    size_t sizeOut = LZ4_compress_default(input->getData()->data, ptrOut, sizeIn, max);
    blockSize=(uint32_t) (sizeOut & 0x7FFF); // highest bit zero when frame is LZ4 compressed
    
    // copy-back result
     if ((sizeOut+lz4FrameFormatBytes<=sizeIn)&&(sizeOut>0)) {
      // compression success
      // and we are able to fit result+headers in same buffer
      
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
      
      // looks good, give it back
      output=input;
      output->getData()->header.dataSize=ptrSize;
      err=0;
    }
    free(ptrOut);
    return err;
  }

}  // extern "C"
