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
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include <string>

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentPlayer : public ReadoutEquipment {

public:
  ReadoutEquipmentPlayer(ConfigFile &cfg, std::string name = "dummyReadout");
  ~ReadoutEquipmentPlayer();
  DataBlockContainerReference getNextBlock();

private:
  Thread::CallbackResult populateFifoOut(); // iterative callback

  std::string filePath = "";        // path to data file
  size_t fileSize = 0;              // data file size
  std::unique_ptr<char[]> fileData; // copy of file content

  int preLoad;  // if set, data preloaded in the memory pool
  int fillPage; // if set, page is filled multiple time

  size_t bytesPerPage = 0; // number of bytes per data page

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

  // log config summary
  theLog.log("Equipment %s: using data source file=%s preLoad=%d fillPage=%d",
             name.c_str(), filePath.c_str(), preLoad, fillPage);

  // open data file
  FILE *fp = fopen(filePath.c_str(), "rb");
  if (fp == nullptr) {
    throw(std::string("open failed: ") + strerror(errno));
  }

  // get file size
  fseek(fp, 0L, SEEK_END);
  fileSize = ftell(fp);
  rewind(fp);
  theLog.log("Loading file = %lu bytes", (unsigned long)fileSize);

  // check memory pool data pages large enough
  size_t usablePageSize = memoryPoolPageSize - sizeof(DataBlock);
  if (usablePageSize < fileSize) {
    throw(std::string("memoryPoolPageSize too small, need at least ") +
          std::to_string(fileSize + sizeof(DataBlock)) + std::string(" bytes"));
  }

  // allocate a buffer
  fileData = std::make_unique<char[]>(fileSize);
  if (fileData == nullptr) {
    throw(std::string("memory allocation failure"));
  }

  // load file
  if (fread(fileData.get(), fileSize, 1, fp) != 1) {
    throw(std::string("Failed to load file"));
  };
  fclose(fp);

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

ReadoutEquipmentPlayer::~ReadoutEquipmentPlayer() {}

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
    b->header.dataSize = bytesPerPage;

    // say it's contiguous header+data
    // todo: align begin of data ?
    b->data = &(((char *)b)[sizeof(DataBlock)]);

    // copy file data to page, if not done already
    if (!preLoad) {
      copyFileDataToPage(b->data);
    }
  }

  return nextBlock;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentPlayer(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentPlayer>(cfg, cfgEntryPoint);
}
