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

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentDummy : public ReadoutEquipment {

public:
  ReadoutEquipmentDummy(ConfigFile &cfg, std::string name = "dummyReadout");
  ~ReadoutEquipmentDummy();
  DataBlockContainerReference getNextBlock();

private:
  Thread::CallbackResult populateFifoOut(); // iterative callback

  int eventMaxSize; // maximum data block size
  int eventMinSize; // minimum data block size
  int fillData;     // if set, data pages filled with incremental values
};

ReadoutEquipmentDummy::ReadoutEquipmentDummy(ConfigFile &cfg,
                                             std::string cfgEntryPoint)
    : ReadoutEquipment(cfg, cfgEntryPoint) {

  // get configuration values
  // configuration parameter: | equipment-dummy-* | eventMaxSize | bytes | 128k
  // | Maximum size of randomly generated event. | configuration parameter: |
  // equipment-dummy-* | eventMinSize | bytes | 128k | Minimum size of randomly
  // generated event. | configuration parameter: | equipment-dummy-* | fillData
  // | int | 0 | Pattern used to fill data page: (0) no pattern used, data page
  // is left untouched, with whatever values were in memory (1) incremental byte
  // pattern (2) incremental word pattern, with one random word out of 5. |
  std::string sBytes;
  eventMaxSize = (int)128 * 1024;
  eventMinSize = (int)128 * 1024;
  if (cfg.getOptionalValue<std::string>(cfgEntryPoint + ".eventMaxSize",
                                        sBytes) == 0) {
    eventMaxSize = ReadoutUtils::getNumberOfBytesFromString(sBytes.c_str());
  }
  if (cfg.getOptionalValue<std::string>(cfgEntryPoint + ".eventMinSize",
                                        sBytes) == 0) {
    eventMinSize = ReadoutUtils::getNumberOfBytesFromString(sBytes.c_str());
  }
  cfg.getOptionalValue<int>(cfgEntryPoint + ".fillData", fillData, (int)0);

  // log config summary
  theLog.log("Equipment %s: eventSize: %d -> %d, fillData=%d", name.c_str(),
             eventMinSize, eventMaxSize, fillData);

  // ensure generated events will fit in blocks allocated from memory pool
  if ((size_t)eventMaxSize > mp->getDataBlockMaxSize()) {
    theLog.log("memoryPoolPageSize too small, need at least %ld bytes",
               (long)(eventMaxSize + mp->getPageSize() - mp->getDataBlockMaxSize()));
    throw __LINE__;
  }
}

ReadoutEquipmentDummy::~ReadoutEquipmentDummy() {}

DataBlockContainerReference ReadoutEquipmentDummy::getNextBlock() {

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
    DataBlock *b = nextBlock->getData();

    // set size
    int dSize = (int)(eventMinSize + (int)((eventMaxSize - eventMinSize) *
                                           (rand() * 1.0 / RAND_MAX)));

    // no need to fill header defaults, this is done by getNewDataBlockContainer()
    // only adjust payload size
    b->header.dataSize = dSize;

    // optionaly fill data range
    if (fillData == 1) {
      // incremental byte pattern
      for (int k = 0; k < dSize; k++) {
        b->data[k] = (char)k;
      }
    } else if (fillData == 2) {
      // incremental word pattern, with one random word out of 5
      int *pi = (int *)b->data;
      for (unsigned int k = 0; k < dSize / sizeof(int); k++) {
        pi[k] = k;
        if (k % 5 == 0)
          pi[k] = rand();
      }
    }
  }

  return nextBlock;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentDummy>(cfg, cfgEntryPoint);
}
