/*
  This processor compresses data with LZ4 algorithm https://lz4.github.io/lz4/ 
*/

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include "lz4.h"
#include <string.h>

extern "C"
{
 
  int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output) {

    int err=-1;
    void *ptr=input->getData()->data; // data input
    if (ptr==NULL) {return -1;}
    size_t size=input->getData()->header.dataSize; // input size (bytes)
    
    int max = LZ4_compressBound(size); // maximum size after compression

    char *buffer=nullptr;
    if (buffer==nullptr) {
      buffer=(char *)malloc(max);
      if (buffer==nullptr) {return -1;}
    }
    
    int compressed_data_size = LZ4_compress_default(input->getData()->data, buffer, size, max);
    
    if ((compressed_data_size<=size)&&(compressed_data_size>0)) {
      // compression success
      memcpy(input->getData()->data,buffer,compressed_data_size);
      output=input;
      output->getData()->header.dataSize=compressed_data_size;
      err=0;
    }
    free(buffer);
    return err;
  }

}  // extern "C"
