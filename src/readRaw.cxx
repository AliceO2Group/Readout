// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <lz4.h>
#include <stdio.h>
#include <string>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "RdhUtils.h"

//#define ERRLOG(args...) fprintf(stderr,args)
#define ERRLOG(args...) fprintf(stdout, args)

int main(int argc, const char* argv[])
{

  // parameters
  std::string filePath;
  typedef enum { plain,
                 lz4,
                 undefined } FileType;
  FileType fileType = FileType::plain;

  bool dumpRDH = false;
  bool validateRDH = true;
  bool dumpDataBlockHeader = false;
  int dumpData = 0;             // if set, number of bytes to dump in each data page
  int dumpDataInline = 0;       // if set, number of bytes to dump in each data page, after RDH dump
  bool fileReadVerbose = false; // flag to print more info (chunk size, etc) when reading file
  bool dataBlockHeaderEnabled = false;
  bool checkContinuousTriggerOrder = false;
  bool isAutoPageSize = false; // flag set when no known page size in file

  uint32_t timeframePeriodOrbits = 0;
  uint32_t firstTimeframeHbOrbitBegin = 0;
  bool isDefinedFirstTimeframeHbOrbitBegin = 0;

  // parse input arguments
  // format is a list of key=value pairs

  if (argc < 2) {
    ERRLOG(
      "Usage: %s [rawFilePath] [options]\n"
      "List of options:\n"
      "    filePath=(string) : path to file.\n"
      "    dataBlockEnabled=0|1: specify if file is with/without internal readout data block headers.\n"
      "    dumpRDH=0|1 : dump the RDH headers.\n"
      "    validateRDH=0|1 : check the RDH headers.\n"
      "    checkContinuousTriggerOrder=0|1 : check trigger order.\n"
      "    dumpDataBlockHeader=0|1 : dump the data block headers (internal readout headers).\n"
      "    dumpData=(int) : dump the data pages. If -1, all bytes. Otherwise, the first bytes only, as specified.\n"
      "    dumpDataInline=(int) : if set, each packet raw content is printed (hex dump style).\n"
      "    fileReadVerbose=(int) : if set, more information is printed when reading/decoding file.\n"
      "    timeframePeriodOrbits=(int) : if set, TF id computed (and printed, when dump enabled) for each RDH. Typically, 128 or 256.\n"
      "    \n",
      argv[0]);
    return -1;
  }

  // extra options
  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    std::string key(option);
    size_t separatorPosition = key.find('=');
    if (separatorPosition == std::string::npos) {
      // if this is the first argument, use it as the file name (for backward compatibility)
      if (i == 1) {
        filePath = argv[i];
      } else {
        ERRLOG("Failed to parse option '%s'\n", option);
      }
      continue;
    }
    key.resize(separatorPosition);
    std::string value = &(option[separatorPosition + 1]);

    if (key == "fileType") {
      if (value == "plain") {
        fileType = FileType::plain;
      } else if (value == "lz4") {
        fileType = FileType::lz4;
      } else {
        ERRLOG("wrong file type %s\n", value.c_str());
      }
    } else if (key == "filePath") {
      filePath = value;
    } else if (key == "dataBlockHeaderEnabled") {
      dataBlockHeaderEnabled = std::stoi(value);
    } else if (key == "dumpRDH") {
      dumpRDH = std::stoi(value);
    } else if (key == "validateRDH") {
      validateRDH = std::stoi(value);
    } else if (key == "dumpDataBlockHeader") {
      dumpDataBlockHeader = std::stoi(value);
    } else if (key == "dumpData") {
      dumpData = std::stoi(value);
    } else if (key == "dumpDataInline") {
      dumpDataInline = std::stoi(value);
    } else if (key == "fileReadVerbose") {
      fileReadVerbose = std::stoi(value);
    } else if (key == "checkContinuousTriggerOrder") {
      checkContinuousTriggerOrder = std::stoi(value);
    } else if (key == "timeframePeriodOrbits") {
      timeframePeriodOrbits = (uint32_t) std::stoi(value);
    } else {
      ERRLOG("unknown option %s\n", key.c_str());
    }
  }

  if (filePath == "") {
    ERRLOG("Please provide a file name\n");
    return -1;
  }

  ERRLOG("Using data file %s\n", filePath.c_str());
  ERRLOG("dataBlockHeaderEnabled=%d dumpRDH=%d validateRDH=%d checkContinuousTriggerOrder=%d dumpDataBlockHeader=%d dumpData=%d dumpDataInline=%d fileReadVerbose=%d \n", (int)dataBlockHeaderEnabled, (int)dumpRDH, (int)validateRDH, (int)checkContinuousTriggerOrder, (int)dumpDataBlockHeader, dumpData, (int)dumpDataInline, (int)fileReadVerbose);

  // open raw data file
  FILE* fp = fopen(filePath.c_str(), "rb");
  if (fp == NULL) {
    ERRLOG("Failed to open file\n");
    return -1;
  }

  // get file size
  long fileSize = 0;
  // read all file in one go: get file size
  if (fseek(fp, 0, SEEK_END)) {
    ERRLOG("Failed to get file size");
    return -1;
  }
  fileSize = ftell(fp);
  if (fileSize < 0) {
    ERRLOG("Failed to get file size");
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET)) {
    ERRLOG("Failed to rewing file");
    return -1;
  }
  if (fileReadVerbose) {
    printf("File size: %ld bytes\n", fileSize);
  }

  // read file
  unsigned long pageCount = 0;
  unsigned long RDHBlockCount = 0;
  unsigned long fileOffset = 0;
  unsigned long dataOffset = 0;                                 // to keep track of position in uncompressed data
  unsigned long dataOffsetLast = 0;                             // to print progress
  const int dataOffsetProgressStep = 1 * 1024L * 1024L * 1024L; // every 1GB, print where we are

  bool isFirstTrigger = true;
  uint32_t latestTriggerOrbit = 0;
  uint32_t latestTriggerBC = 0;

  const int maxBlockSize = 128 * 1024L * 1024L; // maximum memory allocated for page reading (or decompressing)
  bool checkOrbitContiguous = true;             // if set, verify that they are no holes in orbit number

  for (fileOffset = 0; fileOffset < (unsigned long)fileSize;) {

#define ERR_LOOP                                          \
  {                                                       \
    ERRLOG("Error %d @ 0x%08lX\n", __LINE__, fileOffset); \
    break;                                                \
  }

    unsigned long blockOffset = dataOffset;
    long dataSize;

    if (dataBlockHeaderEnabled) {
      DataBlockHeader hb;
      if (fread(&hb, sizeof(hb), 1, fp) != 1) {
        break;
      }
      fileOffset += sizeof(hb);

      if (hb.headerVersion != defaultDataBlockHeader.headerVersion) {
        ERR_LOOP;
      }
      if (hb.headerSize != sizeof(hb)) {
        ERR_LOOP;
      }

      if (dumpDataBlockHeader) {
        printf("Block header %lu @ %lu\n", pageCount + 1, fileOffset - sizeof(hb));
        printf("\theaderVersion= 0x%08X\n", hb.headerVersion);
        printf("\theaderSize = %u\n", hb.headerSize);
        printf("\tdataSize = %u\n", hb.dataSize);
        printf("\tlinkId = %u\n", hb.linkId);
        printf("\tequipmentId = %d\n", (int)hb.equipmentId);
        printf("\ttimeframeId = %llu\n", (unsigned long long)hb.timeframeId);
        printf("\tblockId = %llu\n", (unsigned long long)hb.blockId);
        printf("\tdata @ %lu\n", fileOffset);
      }
      dataSize = hb.dataSize;
    } else {
      dataSize = fileSize - fileOffset;

      if (dataSize > maxBlockSize) {
        dataSize = maxBlockSize;
      }

      if (fileType == FileType::lz4) {
        // read start of LZ4 frame: header + size
        const char header[] = { 0x04, 0x22, 0x4D, 0x18, 0x60, 0x70, 0x73 };
        uint32_t blockSize = 0;
        char buffer[sizeof(header) + sizeof(blockSize)];
        if (fread(&buffer, sizeof(buffer), 1, fp) != 1) {
          ERR_LOOP;
        }

        // check header correct
        bool isHeaderOk = true;
        for (unsigned int i = 0; i < sizeof(header); i++) {
          if (header[i] != buffer[i]) {
            isHeaderOk = false;
            break;
          }
        }
        if (!isHeaderOk) {
          ERR_LOOP;
        }
        blockSize = *((uint32_t*)&buffer[sizeof(header)]);
        // use given LZ4 frame size
        dataSize = blockSize;
      } else {
        isAutoPageSize = true;
      }
    }

    // printf("Reading page %lu @ 0x%08lX (%.1fGB)\n",pageCount+1,fileOffset,fileOffset/(1024.0*1024.0*1024.0));
    if (fileReadVerbose) {
      printf("Reading chunk %lu : %lu bytes @ 0x%08lX - 0x%08lX\n", pageCount + 1, dataSize, fileOffset, fileOffset + dataSize - 1);
    }

    if (dataSize == 0) {
      ERR_LOOP;
    }
    void* data = malloc(dataSize);
    if (data == NULL) {
      ERR_LOOP;
    }
    if (fread(data, dataSize, 1, fp) != 1) {
      ERR_LOOP;
    }
    fileOffset += dataSize;
    pageCount++;

    if (fileType == FileType::lz4) {
      // read trailer
      const char trailer[] = { 0x00, 0x00, 0x00, 0x00 };
      char buffer[sizeof(trailer)];
      if (fread(&buffer, sizeof(buffer), 1, fp) != 1) {
        ERR_LOOP;
      }

      // check trailer correct
      bool istrailerOk = true;
      for (unsigned int i = 0; i < sizeof(trailer); i++) {
        if (trailer[i] != buffer[i]) {
          istrailerOk = false;
          break;
        }
      }
      if (!istrailerOk) {
        ERR_LOOP;
      }

      // uncompress data
      void* dataUncompressed = malloc(maxBlockSize);
      if (dataUncompressed == NULL) {
        ERR_LOOP;
      }
      int res = LZ4_decompress_safe((char*)data, (char*)dataUncompressed, dataSize, maxBlockSize);
      if ((res <= 0) || (res >= maxBlockSize)) {
        ERR_LOOP;
      }
      free(data);
      data = dataUncompressed;
      dataSize = res;
      if (fileReadVerbose) {
        printf("uncompressed = %ld bytes\n", dataSize);
      }
    }

    if (dumpData) {
      long max = dataSize;
      if ((dumpData < max) && (dumpData > 0)) {
        max = dumpData;
      }
      printf("Data page %lu @ %lu (%ld bytes)", pageCount, blockOffset, dataSize);
      for (long i = 0; i < max; i++) {
        if (i % 16 == 0) {
          printf("\n\t");
        }
        printf("%02X ", (int)(((unsigned char*)data)[i]));
      }
      printf("\n\t...\n");
    }

    if ((validateRDH) || (dumpRDH)) {
      std::string errorDescription;
      for (size_t pageOffset = 0; pageOffset < (unsigned long)dataSize;) {

        // check we are not at page boundary
        if (pageOffset + sizeof(o2::Header::RAWDataHeader) > (unsigned long)dataSize) {
          if (isAutoPageSize) {
            // the (virtual) page boundary is in the middle of packet... try to
            // realign
            int delta = dataSize - pageOffset;
            fileOffset -= delta;
            dataSize -= delta;
            if (fseek(fp, fileOffset, SEEK_SET)) {
              ERRLOG("Failed to seek in file");
              return -1;
            }
            if (fileReadVerbose) {
              printf("Realign chunk boundary (header misaligned)\n");
            }
            break;
          }
          ERRLOG("RDH/page header misaligned\n");
        }

        RDHBlockCount++;
        RdhHandle h(((uint8_t*)data) + pageOffset);

        // guess TF id
        if (timeframePeriodOrbits) {
          if (!isDefinedFirstTimeframeHbOrbitBegin) {
	    // use first orbit for TF id computation
	    firstTimeframeHbOrbitBegin = h.getHbOrbit();
	    isDefinedFirstTimeframeHbOrbitBegin = 1;
	  }
	  h.computeTimeframeId(firstTimeframeHbOrbitBegin, timeframePeriodOrbits);
        }

        if (dumpRDH) {
          h.dumpRdh(pageOffset + blockOffset, 1);
        }

        int nErr = h.validateRdh(errorDescription);
        if (nErr) {
          if (!dumpRDH) {
            // dump RDH if not done already
            h.dumpRdh(pageOffset, 1);
          }
          ERRLOG("File offset 0x%08lX + %ld\n%s", blockOffset, pageOffset, errorDescription.c_str());
          errorDescription.clear();

          if (dumpDataInline) {
            // dump remaining of page
            long nBytes = dataSize - pageOffset;
            for (long ix = 0; ix < nBytes; ix++) {
              if (ix % 16 == 0) {
                printf("\n\t0x%04x\t", (int)ix);
              }
              printf("%02X ", (int)(((unsigned char*)data)[pageOffset + ix]));
            }
            printf("\n\n");
          }

          // we can not continue decoding if RDH wrong, we are not sure if we can jump to the next ... or should we used fixed 8kB ?
          break;
        }

        if (checkContinuousTriggerOrder) {
          bool isTriggerOrderOk = true;
          if (isFirstTrigger) {
            isFirstTrigger = false;
          } else {
            if (h.getTriggerOrbit() < latestTriggerOrbit) {
              isTriggerOrderOk = 0;
            } else if (h.getTriggerOrbit() == latestTriggerOrbit) {
              if (h.getTriggerBC() < latestTriggerBC) {
                isTriggerOrderOk = 0;
              }
            } else if (checkOrbitContiguous && (h.getTriggerOrbit() != latestTriggerOrbit + 1)) {
              isTriggerOrderOk = 0;
            }
          }
          if (!isTriggerOrderOk) {
            ERRLOG("Trigger order mismatch@ file offset 0x%08lX + %ld : new %08X : %03X > previous: %08X : %03X \n", blockOffset, pageOffset, h.getTriggerOrbit(), h.getTriggerBC(), latestTriggerOrbit, latestTriggerBC);
          }
          latestTriggerBC = h.getTriggerBC();
          latestTriggerOrbit = h.getTriggerOrbit();
          // printf("%08X : %03X\n", h.getTriggerOrbit(), h.getTriggerBC());
        }

        if (dumpDataInline) {
          long nBytes = h.getOffsetNextPacket();
          for (long ix = 0; ix < nBytes; ix++) {
            if (ix % 16 == 0) {
              printf("\n\t0x%04x\t", (int)ix);
            }
            printf("%02X ", (int)(((unsigned char*)data)[pageOffset + ix]));
          }
          printf("\n\n");
        }

        // go to next RDH
        uint16_t offsetNextPacket = h.getOffsetNextPacket();
        if (offsetNextPacket == 0) {
          break;
        }

        if ((pageOffset + offsetNextPacket > (unsigned long)dataSize) && (pageOffset + offsetNextPacket + fileOffset - dataSize < (unsigned long)fileSize)) {
          if (isAutoPageSize) {
            // the (virtual) page boundary is in the middle of packet... try to realign
            int delta = pageOffset + offsetNextPacket - dataSize;
            fileOffset += delta;
            dataSize += delta;
            if (fseek(fp, fileOffset, SEEK_SET)) {
              ERRLOG("Failed to seek in file");
              return -1;
            }
            if (fileReadVerbose) {
              printf("Realign chunk boundary (payload misaligned)\n");
            }
            break;
          }
          ERRLOG("RDH/page payload misaligned\n");
        }

        pageOffset += offsetNextPacket;
      }
    }

    free(data);
    dataOffset += dataSize;
    if (dataOffsetProgressStep) {
      if (dataOffset > dataOffsetLast + dataOffsetProgressStep) {
        dataOffsetLast = dataOffset;
        printf("Processed %.1fGB\n", dataOffset / (1024.0 * 1024.0 * 1024.0));
      }
    }
  }
  ERRLOG("%lu data pages\n", pageCount);
  if (RDHBlockCount) {
    ERRLOG("%lu RDH blocks\n", RDHBlockCount);
  }
  ERRLOG("%lu bytes\n", fileOffset);

  // check file status
  if (feof(fp)) {
    ERRLOG("End of file\n");
  }

  // close file
  fclose(fp);
}
