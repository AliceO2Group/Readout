// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "MemoryBankManager.h"
#include "RdhUtils.h"
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include <string>

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentPlayer : public ReadoutEquipment {

public:
  ReadoutEquipmentPlayer(ConfigFile &cfg,
                         std::string name = "filePlayerReadout");
  ~ReadoutEquipmentPlayer();
  DataBlockContainerReference getNextBlock();

private:
  void initCounters();

  Thread::CallbackResult populateFifoOut(); // iterative callback

  std::string filePath = "";        // path to data file
  size_t fileSize = 0;              // data file size
  std::unique_ptr<char[]> fileData; // copy of file content

  int preLoad;   // if set, data preloaded in the memory pool
  int fillPage;  // if set, page is filled multiple time
  int autoChunk; // if set, page boundary extracted from RDH info

  size_t bytesPerPage = 0;      // number of bytes per data page
  FILE *fp = nullptr;           // file handle
  bool fpOk = false;            // flag to say if fp can be used
  unsigned long fileOffset = 0; // current file offset

  struct PacketHeader {
    uint64_t timeframeId = 0;
    int linkId = undefinedLinkId;
  };
  PacketHeader lastPacketHeader; // keep track of last packet header

  uint32_t timeframePeriodOrbits =
      256; // timeframe interval duration in number of LHC orbits
  uint32_t firstTimeframeHbOrbitBegin =
      0; // HbOrbit of beginning of first timeframe

  void copyFileDataToPage(void *page); // fill given page with file data
                                       // according to current settings
};

void ReadoutEquipmentPlayer::copyFileDataToPage(void *page) {
  if (page == nullptr)
    return;
  if (fileData == nullptr)
    return;
  if (fileSize == 0)
    return;
  int nCopy = 1;
  if (fillPage) {
    nCopy = bytesPerPage / fileSize;
  }
  char *ptr = (char *)page;
  for (int i = 0; i < nCopy; i++) {
    memcpy(ptr, fileData.get(), fileSize);
    ptr += fileSize;
  }
}

ReadoutEquipmentPlayer::ReadoutEquipmentPlayer(ConfigFile &cfg,
                                               std::string cfgEntryPoint)
    : ReadoutEquipment(cfg, cfgEntryPoint) {

  auto errorHandler = [&](const std::string &err) {
    if (fp != nullptr) {
      fclose(fp);
    }
    throw err;
  };

  // get configuration values
  // configuration parameter: | equipment-player-* | filePath | string | | Path
  // of file containing data to be injected in readout. |
  filePath = cfg.getValue<std::string>(cfgEntryPoint + ".filePath");
  // configuration parameter: | equipment-player-* | preLoad | int | 1 | If 1,
  // data pages preloaded with file content on startup. If 0, data is copied at
  // runtime. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".preLoad", preLoad, 1);
  // configuration parameter: | equipment-player-* | fillPage | int | 1 | If 1,
  // content of data file is copied multiple time in each data page until page
  // is full (or almost full: on the last iteration, there is no partial copy if
  // remaining space is smaller than full file size). If 0, data file is copied
  // exactly once in each data page. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".fillPage", fillPage, 1);
  // configuration parameter: | equipment-player-* | autoChunk | int | 0 | When
  // set, the file is replayed once, and cut automatically in data pages
  // compatible with memory bank settings and RDH information.
  // In this mode the preLoad and fillPage options have no effect. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".autoChunk", autoChunk, 0);
  // configuration parameter: | equipment-player-* | TFperiod | int | 256 |
  // Duration of a timeframe, in number of LHC orbits. |
  int cfgTFperiod = 256;
  cfg.getOptionalValue<int>(name + ".TFperiod", cfgTFperiod);
  timeframePeriodOrbits = cfgTFperiod;

  // log config summary
  theLog.log("Equipment %s: using data source file=%s preLoad=%d fillPage=%d "
             "autoChunk=%d TFperiod=%d",
             name.c_str(), filePath.c_str(), preLoad, fillPage, autoChunk,
             timeframePeriodOrbits);

  // open data file
  fp = fopen(filePath.c_str(), "rb");
  if (fp == nullptr) {
    errorHandler(std::string("open failed: ") + strerror(errno));
  }
  fpOk = true;

  // get file size
  if (fseek(fp, 0L, SEEK_END) < 0) {
    errorHandler(std::string("seek failed: ") + strerror(errno));
  }
  long fs = ftell(fp);
  if (fs < 0) {
    errorHandler(std::string("ftell failed: ") + strerror(errno));
  }
  if (fs == 0) {
    errorHandler(std::string("file is empty"));
  }
  fileSize = (size_t)fs;

  // reset counters
  initCounters();

  if (autoChunk) {
    bytesPerPage = memoryPoolPageSize - sizeof(DataBlock);
    theLog.log("Will load file = %lu bytes in chunks of maximum %lu bytes",
               (unsigned long)fileSize, (unsigned long)bytesPerPage);
    return;
  }

  theLog.log("Loading file = %lu bytes", (unsigned long)fileSize);

  // check memory pool data pages large enough
  size_t usablePageSize = memoryPoolPageSize - sizeof(DataBlock);
  if (usablePageSize < fileSize) {
    errorHandler(std::string("memoryPoolPageSize too small, need at least ") +
                 std::to_string(fileSize + sizeof(DataBlock)) +
                 std::string(" bytes"));
  }

  // allocate a buffer
  fileData = std::make_unique<char[]>(fileSize);
  if (fileData == nullptr) {
    errorHandler(std::string("memory allocation failure"));
  }

  // load file
  if (fread(fileData.get(), fileSize, 1, fp) != 1) {
    errorHandler(std::string("Failed to load file"));
  };
  fclose(fp);
  fp = nullptr;
  fpOk = false;

  // init variables
  if (fillPage) {
    bytesPerPage = (usablePageSize / fileSize) * fileSize;
  } else {
    bytesPerPage = fileSize;
  }
  theLog.log("Data page size used = %zu bytes", bytesPerPage);

  // preload data to pages
  if (preLoad) {
    std::vector<DataBlockContainerReference> dataPages;
    for (;;) {
      DataBlockContainerReference nextBlock = mp->getNewDataBlockContainer();
      if (nextBlock == nullptr) {
        break;
      }
      char *ptr = &(((char *)nextBlock->getData())[sizeof(DataBlock)]);
      copyFileDataToPage(ptr);
      dataPages.push_back(nextBlock);
    }
    theLog.log("%lu pages have been pre-loaded with data from file",
               dataPages.size());
    dataPages.clear();
  }
}

ReadoutEquipmentPlayer::~ReadoutEquipmentPlayer() {
  if (fp != nullptr) {
    fclose(fp);
  }
}

DataBlockContainerReference ReadoutEquipmentPlayer::getNextBlock() {
  // query memory pool for a free block
  DataBlockContainerReference nextBlock = nullptr;
  try {
    nextBlock = mp->getNewDataBlockContainer();
  } catch (...) {
  }

  // format data block
  if (nextBlock != nullptr) {
    DataBlock *b = nextBlock->getData();

    // fill header
    b->header.blockType = DataBlockType::H_BASE;
    b->header.headerSize = sizeof(DataBlockHeaderBase);
    b->header.dataSize = 0;

    // say it's contiguous header+data
    // todo: align begin of data ?
    b->data = &(((char *)b)[sizeof(DataBlock)]);

    if (autoChunk) {
      bool isOk = 1;
      // read from file
      if ((fp != nullptr) && (fpOk)) {
        size_t nBytes = fread(b->data, 1, bytesPerPage, fp);
        if (nBytes == 0) {
          if (ferror(fp)) {
            theLog.log(InfoLogger::Severity::Error,
                       "File %s read error, aborting replay", name.c_str());
          }
          if (feof(fp)) {
            theLog.log("File %s replay completed", name.c_str());
          }
          isOk = 0;
        } else {
          // printf ("read %d bytes\n",nBytes);
          // scan the data to find a page boundary
          size_t pageOffset = 0;
          for (; pageOffset < nBytes;) {
            if (pageOffset + sizeof(o2::Header::RAWDataHeader) > nBytes) {
              break;
            }
            RdhHandle h(((uint8_t *)b->data) + pageOffset);
            std::string errorDescription;
            int nErr = h.validateRdh(errorDescription);
            if (nErr) {
              theLog.log(InfoLogger::Severity::Error,
                         "File %s RDH error, aborting replay @ 0x%lX: %s",
                         name.c_str(), (unsigned long)(fileOffset + pageOffset),
                         errorDescription.c_str());
              isOk = 0;
              break;
            }
            // printf ("RDH @ %lu+ %d\n",fileOffset,pageOffset);
            PacketHeader currentPacketHeader;
            currentPacketHeader.linkId = (int)h.getLinkId();
            bool isFirst = (fileOffset == 0) && (pageOffset == 0);

            int hbOrbit = h.getHbOrbit();
            ;
            if (isFirst) {
              firstTimeframeHbOrbitBegin = hbOrbit;
            }
            currentPacketHeader.timeframeId =
                1 +
                (hbOrbit - firstTimeframeHbOrbitBegin) / timeframePeriodOrbits;

            // fill page metadata
            if (pageOffset == 0) {
              // printf("link %d TF
              // %d\n",(int)currentPacketHeader.linkId,(int)currentPacketHeader.timeframeId);
              b->header.linkId = currentPacketHeader.linkId;
              b->header.timeframeId = currentPacketHeader.timeframeId;
            }

            // changing link or TF -> change page
            bool changePage = 0;
            if (!isFirst) {
              if ((currentPacketHeader.linkId != lastPacketHeader.linkId) ||
                  (currentPacketHeader.timeframeId !=
                   lastPacketHeader.timeframeId)) {
                // printf("%d : %d -> %d :
                // %d\n",currentPacketHeader.linkId,currentPacketHeader.timeframeId,lastPacketHeader.linkId,lastPacketHeader.timeframeId);
                changePage = 1;
              }
            }
            lastPacketHeader = currentPacketHeader;
            if (changePage) {
              // printf("force new page\n");
              break;
            }

            uint16_t offsetNextPacket = h.getOffsetNextPacket();
            if (offsetNextPacket == 0) {
              break;
            }
            if (pageOffset + offsetNextPacket > nBytes) {
              break;
            }
            pageOffset += offsetNextPacket;
          }

          if (pageOffset == 0) {
            theLog.log(InfoLogger::Severity::Error,
                       "File %s stopping replay @ 0x%lX, last packet invalid",
                       name.c_str(), (unsigned long)(fileOffset + pageOffset));
            isOk = 0;
          }
          int delta = nBytes - pageOffset;
          nBytes = pageOffset;
          b->header.dataSize = nBytes;
          fileOffset += nBytes;
          // printf ("bytes = %d    delta = %d    new file Offset = %lu\n",
          // nBytes, delta, fileOffset);
          if (delta > 0) {
            // rewind if necessary
            if (fseek(fp, fileOffset, SEEK_SET)) {
              theLog.log(InfoLogger::Severity::Error,
                         "Failed to seek in file, aborting replay");
              isOk = 0;
            }
          }
        }
      } else {
        isOk = 0;
      }
      if (!isOk) {
        fpOk = false;
        return nullptr;
      }
    } else {
      // copy file data to page, if not done already
      if (!preLoad) {
        copyFileDataToPage(b->data);
      }
    }
  }

  return nextBlock;
}

void ReadoutEquipmentPlayer::initCounters() {
  fpOk = false;
  if (fp != nullptr) {
    if (fseek(fp, 0L, SEEK_SET) != 0) {
      theLog.log(InfoLogger::Severity::Error,
                 "Failed to rewind file, aborting replay");
    } else {
      fpOk = true;
    }
  }
  fileOffset = 0;
  lastPacketHeader.timeframeId = 0;
  lastPacketHeader.linkId = undefinedLinkId;
  firstTimeframeHbOrbitBegin = 0;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentPlayer(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentPlayer>(cfg, cfgEntryPoint);
}
