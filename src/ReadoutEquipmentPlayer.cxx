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

#include <string>

#include "MemoryBankManager.h"
#include "RdhUtils.h"
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include "readoutInfoLogger.h"

class ReadoutEquipmentPlayer : public ReadoutEquipment
{

 public:
  ReadoutEquipmentPlayer(ConfigFile& cfg, std::string name = "filePlayerReadout");
  ~ReadoutEquipmentPlayer();
  DataBlockContainerReference getNextBlock();

 private:
  void initCounters();

  Thread::CallbackResult populateFifoOut(); // iterative callback

  std::string filePath = "";        // path to data file
  size_t fileSize = 0;              // data file size
  std::unique_ptr<char[]> fileData; // copy of file content

  int preLoad;       // if set, data preloaded in the memory pool
  int fillPage;      // if set, page is filled multiple time
  int autoChunk;     // if set, page boundary extracted from RDH info
  int autoChunkLoop; // if set, file is replayed in loop

  size_t bytesPerPage = 0;      // number of bytes per data page
  FILE* fp = nullptr;           // file handle
  std::mutex fpLock;
  bool fpOk = false;            // flag to say if fp can be used
  unsigned long fileOffset = 0; // current file offset
  uint64_t loopCount = 0;       // number of file reading loops so far

  struct PacketHeader {
    uint64_t timeframeId = undefinedTimeframeId;
    int linkId = undefinedLinkId;
    int equipmentId = undefinedEquipmentId; // used to store CRU id
  };
  PacketHeader lastPacketHeader; // keep track of last packet header

  uint32_t orbitOffset = 0; // to be applied to orbit after 1st loop
  int cfgUpdateOrbits = 1; // when set, all RDHs are modified to update orbit, according to orbitOffset

  void copyFileDataToPage(void* page); // fill given page with file data according to current settings
};

void ReadoutEquipmentPlayer::copyFileDataToPage(void* page)
{
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
  char* ptr = (char*)page;
  for (int i = 0; i < nCopy; i++) {
    memcpy(ptr, fileData.get(), fileSize);
    ptr += fileSize;
  }
}

ReadoutEquipmentPlayer::ReadoutEquipmentPlayer(ConfigFile& cfg, std::string cfgEntryPoint) : ReadoutEquipment(cfg, cfgEntryPoint, 1)  // this is RDH-data equipment
{

  auto errorHandler = [&](const std::string& err) {
    if (fp != nullptr) {
      fclose(fp);
      fp = nullptr;
    }
    throw err;
  };

  // get configuration values
  // configuration parameter: | equipment-player-* | filePath | string | | Path of file containing data to be injected in readout. |
  filePath = cfg.getValue<std::string>(cfgEntryPoint + ".filePath");
  // configuration parameter: | equipment-player-* | preLoad | int | 1 | If 1, data pages preloaded with file content on startup. If 0, data is copied at runtime. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".preLoad", preLoad, 1);
  // configuration parameter: | equipment-player-* | fillPage | int | 1 | If 1, content of data file is copied multiple time in each data page until page is full (or almost full: on the last iteration, there is no partial copy if remaining space is smaller than full file size). If 0, data file is copied exactly once in each data page. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".fillPage", fillPage, 1);
  // configuration parameter: | equipment-player-* | autoChunk | int | 0 | When set, the file is replayed once, and cut automatically in data pages compatible with memory bank settings and RDH information. In this mode the preLoad and fillPage options have no effect. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".autoChunk", autoChunk, 0);
  // configuration parameter: | equipment-player-* | autoChunkLoop | int | 0 | When set, the file is replayed in loops. If value is negative, only that number of loop is executed (-5 -> 5x replay). |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".autoChunkLoop", autoChunkLoop, 0);
  // configuration parameter: | equipment-player-* | updateOrbits | int | 1 | When set, trigger orbit counters in all RDH are modified for iterations after the first one (in file loop replay mode), so that they keep increasing. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".updateOrbits", cfgUpdateOrbits, 1);

  // log config summary
  theLog.log(LogInfoDevel_(3002), "Equipment %s: using data source file=%s preLoad=%d fillPage=%d autoChunk=%d autoChunkLoop=%d updateOrbits=%d", name.c_str(), filePath.c_str(), preLoad, fillPage, autoChunk, autoChunkLoop, cfgUpdateOrbits);
  if ((!cfgUpdateOrbits)&&(autoChunkLoop)) {
    theLog.log(LogWarningDevel_(3104), "Equipment %s: RDH orbits auto-update is disabled, generated data will be inconsistent (TFid and orbit counters mismatch)", name.c_str());
  }

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
    bytesPerPage = mp->getDataBlockMaxSize();
    theLog.log(LogInfoDevel, "Will load file = %lu bytes in chunks of maximum %lu bytes", (unsigned long)fileSize, (unsigned long)bytesPerPage);
    return;
  }

  theLog.log(LogInfoDevel, "Loading file = %lu bytes", (unsigned long)fileSize);

  // check memory pool data pages large enough
  size_t usablePageSize = mp->getDataBlockMaxSize();
  if (usablePageSize < fileSize) {
    errorHandler(std::string("memoryPoolPageSize too small, need at least ") + std::to_string(fileSize + mp->getPageSize() - mp->getDataBlockMaxSize()) + std::string(" bytes"));
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
  theLog.log(LogInfoDevel, "Data page size used = %zu bytes", bytesPerPage);

  // preload data to pages
  if (preLoad) {
    std::vector<DataBlockContainerReference> dataPages;
    for (;;) {
      DataBlockContainerReference nextBlock = mp->getNewDataBlockContainer();
      if (nextBlock == nullptr) {
        break;
      }
      char* ptr = nextBlock->getData()->data;
      copyFileDataToPage(ptr);
      dataPages.push_back(nextBlock);
    }
    theLog.log(LogInfoDevel, "%lu pages have been pre-loaded with data from file", dataPages.size());
    dataPages.clear();
  }
}

ReadoutEquipmentPlayer::~ReadoutEquipmentPlayer()
{
  abortThread();
  fpLock.lock();
  if (fp != nullptr) {
    fclose(fp);
    fp = nullptr;
  }
  fpLock.unlock();
}

DataBlockContainerReference ReadoutEquipmentPlayer::getNextBlock()
{
  if (!isDataOn) {
    return nullptr;
  }

  // query memory pool for a free block
  DataBlockContainerReference nextBlock = nullptr;
  try {
    nextBlock = mp->getNewDataBlockContainer();
  } catch (...) {
  }

  // format data block
  if (nextBlock != nullptr) {
    DataBlock* b = nextBlock->getData();

    // no need to fill header defaults, this is done by getNewDataBlockContainer()
    // only adjust payload size
    b->header.dataSize = 0;
    // and possibly set orbit offset
    if (!cfgUpdateOrbits) {
      b->header.orbitOffset = orbitOffset;
    }

    if (autoChunk) {
      bool isOk = 1;
      // read from file
      fpLock.lock();
      if ((fp != nullptr) && (fpOk)) {
        size_t nBytes = fread(b->data, 1, bytesPerPage, fp);
        if (nBytes == 0) {
          isOk = 0;
          if (ferror(fp)) {
            theLog.log(LogErrorSupport_(3232), "File %s read error, aborting replay", name.c_str());
          }
          if (feof(fp)) {
            if ((!autoChunkLoop) || ((loopCount + 1 + autoChunkLoop) == 0)) {
              theLog.log(LogInfoDevel, "File %s replay completed (%lu loops)", name.c_str(), (unsigned long)(loopCount + 1));
            } else {
              // replay file
              if (fseek(fp, 0, SEEK_SET)) {
                theLog.log(LogErrorSupport_(3232), "Failed to rewind file, aborting replay");
              } else {
                if (loopCount == 0) {
                  theLog.log(LogInfoDevel, "File %s replay - 1st loop completed", name.c_str());
                }
                loopCount++;
                fileOffset = 0;
                orbitOffset = lastPacketHeader.timeframeId * getTimeframePeriodOrbits();
		// printf("loop %d: offset = %X\n",(int)loopCount, (int)orbitOffset);
                isOk = 1;
              }
            }
          }
        } else {
          // printf ("read %d bytes\n",nBytes);
          // scan the data to find a page boundary
          size_t pageOffset = 0;
          for (; pageOffset < nBytes;) {
            if (pageOffset + sizeof(o2::Header::RAWDataHeader) > nBytes) {
              break;
            }
            RdhHandle h(((uint8_t*)b->data) + pageOffset);
            std::string errorDescription;
            int nErr = h.validateRdh(errorDescription);
            if (nErr) {
              theLog.log(LogErrorSupport_(3004), "File %s RDH error, aborting replay @ 0x%lX: %s", name.c_str(), (unsigned long)(fileOffset + pageOffset), errorDescription.c_str());
              isOk = 0;
              break;
            }
            if (cfgUpdateOrbits) {
              // update RDH orbit when applicable
              h.incrementHbOrbit(orbitOffset);
            }

            // printf ("RDH @ %lu+ %d\n",fileOffset,pageOffset);
            PacketHeader currentPacketHeader;
            currentPacketHeader.linkId = (int)h.getLinkId();
            currentPacketHeader.equipmentId = (int)(h.getCruId() * 10 + h.getEndPointId());

            int hbOrbit = h.getHbOrbit() + b->header.orbitOffset;
            currentPacketHeader.timeframeId = getTimeframeFromOrbit(hbOrbit);

            // fill page metadata
            if (pageOffset == 0) {
              // printf("eq %d link %d TF %d\n", (int)currentPacketHeader.equipmentId, (int)currentPacketHeader.linkId,(int)currentPacketHeader.timeframeId);
              b->header.linkId = currentPacketHeader.linkId;
              b->header.equipmentId = currentPacketHeader.equipmentId;
              b->header.timeframeId = currentPacketHeader.timeframeId;
            }

            // changing link/cruid or TF -> change page (unless at the beginning of the page)
            bool changePage = 0;
            if (pageOffset != 0) {
              if ((currentPacketHeader.linkId != lastPacketHeader.linkId) || (currentPacketHeader.equipmentId != lastPacketHeader.equipmentId) || (currentPacketHeader.timeframeId != lastPacketHeader.timeframeId)) {
                // printf("new page : %d.%d : %d -> %d.%d : %d\n",currentPacketHeader.equipmentId, currentPacketHeader.linkId,(int)currentPacketHeader.timeframeId,lastPacketHeader.equipmentId,lastPacketHeader.linkId,(int)lastPacketHeader.timeframeId);
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
            theLog.log(LogErrorSupport_(3004), "File %s stopping replay @ 0x%lX, last packet invalid", name.c_str(), (unsigned long)(fileOffset + pageOffset));
            isOk = 0;
          }
          int delta = nBytes - pageOffset;
          nBytes = pageOffset;
          b->header.dataSize = nBytes;
          fileOffset += nBytes;
          // printf ("bytes = %d    delta = %d    new file Offset = %lu\n", nBytes, delta, fileOffset);
          if (delta > 0) {
            // rewind if necessary
            if (fseek(fp, fileOffset, SEEK_SET)) {
              theLog.log(LogErrorSupport_(3232), "Failed to seek in file, aborting replay");
              isOk = 0;
            }
          }
        }
      } else {
        isOk = 0;
      }
      fpLock.unlock();
      if (!isOk) {
        fpOk = false;
        return nullptr;
      }
    } else {
      // copy file data to page, if not done already
      if (!preLoad) {
        copyFileDataToPage(b->data);
      }
      // update header metadata
      b->header.dataSize = bytesPerPage;
    }
    
    // filter out empty pages
    if (b->header.dataSize == 0) {
      nextBlock = nullptr;
    }

  }

  
  return nextBlock;
}

void ReadoutEquipmentPlayer::initCounters()
{
  fpOk = false;
  if (fp != nullptr) {
    if (fseek(fp, 0L, SEEK_SET) != 0) {
      theLog.log(LogErrorSupport_(3232), "Failed to rewind file, aborting replay");
    } else {
      fpOk = true;
    }
  }
  fileOffset = 0;
  loopCount = 0;
  lastPacketHeader.timeframeId = undefinedTimeframeId;
  lastPacketHeader.linkId = undefinedLinkId;
  lastPacketHeader.equipmentId = undefinedEquipmentId;
  orbitOffset = 0;
}

std::unique_ptr<ReadoutEquipment> getReadoutEquipmentPlayer(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ReadoutEquipmentPlayer>(cfg, cfgEntryPoint); }

