// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <string.h>
#include <zlib.h>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"

extern "C" {

int processBlock(DataBlockContainerReference &input, DataBlockContainerReference &output) {

  void *ptr = input->getData()->data; // data input
  if (ptr == NULL) {
    return -1;
  }
  size_t size = input->getData()->header.dataSize; // input size (bytes)

  output = nullptr;

  // zlib struct
  z_stream defstream;
  defstream.zalloc = Z_NULL;
  defstream.zfree = Z_NULL;
  defstream.opaque = Z_NULL;

  deflateInit(&defstream, Z_BEST_SPEED);                    // or Z_BEST_COMPRESSION
  size_t maxSizeOut = deflateBound(&defstream, (uInt)size); // maximum size of output

  // in-place compression does not work completely, just few first bytes are wrong, too bad we have to create a temporary output buffer
  void *out;
  out = malloc(maxSizeOut);
  if (out == nullptr)
    return -1;

  // deflate data page in one go
  defstream.avail_in = (uInt)size;   // size of input
  defstream.next_in = (Bytef *)ptr;  // input
  defstream.avail_out = (uInt)size;  // size of output
  defstream.next_out = (Bytef *)out; // output
  deflate(&defstream, Z_FINISH);
  deflateEnd(&defstream);

  // printf("Compressed size is: %lu - %.2lf\n", defstream.total_out,(defstream.total_in-defstream.total_out)*100.0/defstream.total_in);

  // copy output buffer back to same data page if space allows, otherwise drop it
  if (defstream.total_out > size) {
    return -1;
  }
  memcpy(ptr, out, defstream.total_out);
  free(out);
  output = input;
  output->getData()->header.dataSize = defstream.total_out;

  return 0;
}

} // extern "C"
