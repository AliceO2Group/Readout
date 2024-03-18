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



// This utility does timeframe building on a set of RAW data files and output a single merged RAW data file
// Input files must have a certain level of synchronization: same timeframes in all files, in same order.


#include <lz4.h>
#include <stdio.h>
#include <string>
#include <inttypes.h>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "RdhUtils.h"
#include "CounterStats.h"

#include <filesystem>
#include <string.h>


struct RawFileDescriptor {
  std::string path; // path to file
  FILE *fp = NULL; // file descriptor
  void *buffer = NULL; // memory buffer to read file chunk by chunk
  uint64_t bufferUsed; // amount of buffer in use (filled with data from file)
  uint64_t bufferProcessed; // number of bytes in buffer already processed
  long fileSize; // size of file on disk
  long fileOffset; // current location in file
  uint64_t currentTimeframe; // current timeframe
  uint64_t nextTimeframe; // next timeframe detected
  long bytesOut; // number of bytes written
  bool done; // flag set when file read completed
};

int main(int argc, const char* argv[])
{
  std::vector<std::string> filenames;
  std::vector<RawFileDescriptor> fds;
  bool isError = 0;
  
  std::string outputFile = "/tmp/out.raw"; // path to output merged file
  long bufferSize = 1000000; // chunck size for reading in memory
  bool fileReadVerbose = true; // flag to print more info (chunk size, etc) when reading file
  uint64_t TFperiod = 32; // period of a timeframe
  long totalSize = 0; // input files total size (bytes)
  
  // parse arguments (key=value pairs) and build list of input files
  for (int i = 1; i < argc; i++) {
    
    // check if argument is an option of the form key=value
    const char* option = argv[i];   
    std::string key(option);
    size_t separatorPosition = key.find('=');
    if (separatorPosition != std::string::npos) {
      key.resize(separatorPosition);
      std::string value = &(option[separatorPosition + 1]);
      
      if (key == "outputFile") {
        outputFile = value;
      } else if (key == "bufferSize") {
        bufferSize = std::atoi(value.c_str());
      } else {
        printf("unknown option %s\n", key.c_str());
	isError = 1;
      }
      
      continue;      
    }
    
    filenames.push_back(option);
  }

  // check success
  if (isError) {
    printf("Aborting\n");
    return -1;
  }
  
  // summary of options
  printf("Using options:\n\t outputFile = %s\n\t bufferSize = %lu\n", outputFile.c_str(), (long unsigned)bufferSize);
  
  // open files and init
  // not done in arg paring loop, so that we now have all options set
  for (const auto &fn : filenames) {
    FILE *fp = fopen(fn.c_str(), "rb");
    if (fp == NULL) {
      printf("Can't open %s\n", fn.c_str());
      isError = 1 ;
      continue;
    }
    printf("%s\n", fn.c_str());
    
     // get file size
    long fileSize = std::filesystem::file_size(fn);
    if (fileReadVerbose) {
      printf("File size: %ld bytes\n", fileSize);
    }
    totalSize += fileSize;

    void *buffer = malloc(bufferSize);
    if (buffer == NULL) {
      printf("Failed to allocate buffer\n");
      isError = 1;
      break;
    }

    fds.push_back({.path = fn, .fp = fp, .buffer = buffer, .bufferUsed = 0, .bufferProcessed = 0, .fileSize = fileSize, .fileOffset = 0, .currentTimeframe = 0, .nextTimeframe = 0, .bytesOut = 0, .done = 0});
  }
  
  // check success
  if (isError) {
    printf("Aborting\n");
    return -1;
  }
  
  // open output file
  printf("Opening %s for output\n", outputFile.c_str());
  FILE *fdout = fopen(outputFile.c_str(), "w");
  if (fdout == NULL) {
    printf("Can't open %s for writing\n", outputFile.c_str());
    return -1;
  }
  printf("Expected output size: %ld\n", totalSize);
  
  for(;;) {
   
    unsigned int nCompleted = 0;
    printf("\n\n\n*** LOOP\n");

    // are all files at the same TF now ?
    bool sameTimeframeId = true;
    uint64_t nextTF=0;
    for(auto &fd : fds) {
      if (fd.fileOffset >= fd.fileSize) continue;
      
      if (nextTF == 0) {
        nextTF=fd.nextTimeframe;
      }

      if (fd.nextTimeframe != nextTF) {
      
        sameTimeframeId = 0;
	printf("TF %d != %d @ file %s\n", (int) fd.nextTimeframe, (int) nextTF, fd.path.c_str());
        break;	
      }
    }
    

    for(auto &fd : fds) {
      printf("\nFile %s\n",fd.path.c_str());
      
      bool skip=0;
      for (; !fd.done; ) {

        if ((fd.bufferUsed == 0)||(fd.bufferUsed == fd.bufferProcessed)) {
	  // read new chunk

	  long dataSize = fd.fileSize - fd.fileOffset;
	  if (dataSize > bufferSize) {
            dataSize = bufferSize;
	  }

	  if (fread(fd.buffer, dataSize, 1, fd.fp) != 1) {
	    break;      
	  }
	  printf("Got block %ld bytes @ %ld (total: %ld /%ld)\n", dataSize, fd.fileOffset, fd.fileOffset + dataSize, fd.fileSize);

	  fd.bufferUsed = dataSize;
	  fd.bufferProcessed = 0;
	  fd.fileOffset += dataSize;
	} else {
	    printf("Continuing with buffer @ %ld (%ld /%ld) \n",fd.fileOffset, fd.bufferProcessed, fd.bufferUsed);
	}

        uint64_t bufferProcessedInIteration = 0;
	
	// process current chunk until next timeframe
	while (fd.bufferProcessed < fd.bufferUsed) {

          // check we are not at page boundary
          if (fd.bufferProcessed + sizeof(o2::Header::RAWDataHeader) <= fd.bufferUsed) {
	    
             RdhHandle h(((uint8_t*)fd.buffer) + fd.bufferProcessed);
	     
	     std::string err;
             if (h.validateRdh(err)) {
               printf("RDH error @ %ld: %s", (long)fd.bufferProcessed, err.c_str());
               return -1;
	     }
	     
             long nBytes = h.getOffsetNextPacket();
	     
	     if (fd.bufferProcessed + nBytes <= fd.bufferUsed) {
	       uint64_t TFid = 1 + h.getHbOrbit() / TFperiod;
	       
	       if (TFid != fd.currentTimeframe) {
	         if (TFid != fd.nextTimeframe) {
		   printf("Next TF detected %ld @ %ld\n", TFid, fd.bufferProcessed);
		   if (sameTimeframeId) {
		     // wait that all files are at the same TF before checking next
		     fd.nextTimeframe = TFid;
		   }
		   skip = 1;
		   break;
		 } else {		   
		   if (!sameTimeframeId) {
		     skip = 1;
		     break;
		   }
		   fd.currentTimeframe = TFid; // we can start with this one
		   printf("Starting new TF %ld @ %ld\n", fd.currentTimeframe, fd.bufferProcessed);
		   skip=0;
		 }		 
	       }
               //    h.dumpRdh(fd.fileOffset + fd.bufferProcessed - fd.bufferUsed, 1);
               bufferProcessedInIteration += nBytes;
	       fd.bufferProcessed += nBytes;
	       continue;
	     }
	  }
	  
	  if (!skip) {
            // rewind a bit
            int delta = fd.bufferUsed - fd.bufferProcessed;
	    if (delta) {
	      fd.fileOffset -= delta;
	      printf("%ld / %ld : %d -> new position %ld\n", fd.bufferProcessed, fd.bufferUsed, delta, fd.fileOffset);
	      if (fseek(fd.fp, fd.fileOffset, SEEK_SET)) {
        	printf("Failed to seek in file");
        	return -1;
              }
	    }
	    fd.bufferUsed = 0; // re-read from file from beginning of chunk
	  }
        }
	
	// write validated data
	if (bufferProcessedInIteration) { 
          if (fwrite(&((char *)fd.buffer)[fd.bufferProcessed - bufferProcessedInIteration], bufferProcessedInIteration, 1, fdout)!=1) {
	    printf("Failed to write %d bytes\n",(int) fd.bufferProcessed);
	    printf("%s\n",strerror(errno));
	    return -1;
	  }
          printf("Wrote %d bytes\n", (int)bufferProcessedInIteration);
	  fd.bytesOut += bufferProcessedInIteration;
	}
	if (skip) {
	  printf("skipping until next loop \n");
	  break;
	}
      }
      if ((fd.fileOffset >= fd.fileSize)&&(fd.bufferUsed == fd.bufferProcessed)) {
          fd.done = 1;
          printf("File read completed %ld %ld\n",fd.bufferUsed,fd.bufferProcessed);
          nCompleted++;
      }
    }
    

   printf("*** %d / %ld completed\n", nCompleted, fds.size());
   if (nCompleted == fds.size()) {
     // all files read
     break;
   }

  }
  
  fclose(fdout);
  
  long totalBytesOut = 0;
  for(auto &fd : fds) {
      printf("\nFile %s: %ld / %ld\n",fd.path.c_str(),fd.bytesOut, fd.fileSize);
      totalBytesOut += fd.bytesOut;
  }
  if (totalBytesOut!=totalSize) {
    printf("Warning: output size mismatch input %ld != %ld\n",totalBytesOut, totalSize);
  }
  return 0;

}
