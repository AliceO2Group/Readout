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

#include "ReadoutEquipment.h"
#include "ReadoutStats.h"
#include "readoutInfoLogger.h"
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <chrono>

extern tRunNumber occRunNumber;

ReadoutEquipment::ReadoutEquipment(ConfigFile& cfg, std::string cfgEntryPoint, bool setRdhEquipment)
{

  // example: browse config keys
  // for (auto cfgKey : ConfigFileBrowser (&cfg,"",cfgEntryPoint)) {
  //   std::string cfgValue=cfg.getValue<std::string>(cfgEntryPoint + "." + cfgKey);
  //   printf("%s.%s = %s\n",cfgEntryPoint.c_str(),cfgKey.c_str(),cfgValue.c_str());
  //}

  // by default, name the equipment as the config node entry point
  // configuration parameter: | equipment-* | name | string| | Name used to identify this equipment (in logs). By default, it takes the name of the configuration section, equipment-xxx |
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".name", name, cfgEntryPoint);

  // change defaults for equipments generating data with RDH
  if (setRdhEquipment) {
    theLog.log(LogInfoDevel_(3002), "Equipment %s: generates data with RDH, using specific defaults", name.c_str());
    isRdhEquipment = true;
    cfgRdhUseFirstInPageEnabled = 1; // by default, use first RDH in page
  }

  // configuration parameter: | equipment-* | id | int| | Optional. Number used to identify equipment (used e.g. in file recording). Range 1-65535.|
  int cfgEquipmentId = undefinedEquipmentId;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".id", cfgEquipmentId);
  id = (uint16_t)cfgEquipmentId; // int to 16-bit value

  // configuration parameter: | readout | rate | double | -1 | Data rate limit, per equipment, in Hertz. -1 for unlimited. |
  cfg.getOptionalValue<double>("readout.rate", readoutRate, -1.0);

  // configuration parameter: | equipment-* | idleSleepTime | int | 200 | Thread idle sleep time, in microseconds. |
  cfgIdleSleepTime = 200;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".idleSleepTime", cfgIdleSleepTime);

  // size of equipment output FIFO
  // configuration parameter: | equipment-* | outputFifoSize | int | -1 | Size of output fifo (number of pages). If -1, set to the same value as memoryPoolNumberOfPages (this ensures that nothing can block the equipment while there are free pages). |
  int cfgOutputFifoSize = -1;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".outputFifoSize", cfgOutputFifoSize);

  // get memory bank parameters
  // configuration parameter: | equipment-* | memoryBankName | string | | Name of bank to be used. By default, it uses the first available bank declared. |
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryBankName", memoryBankName);
  std::string cfgMemoryPoolPageSize = "";
  // configuration parameter: | equipment-* | memoryPoolPageSize | bytes | | Size of each memory page to be created. Some space might be kept in each page for internal readout usage. |
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryPoolPageSize", cfgMemoryPoolPageSize);
  // configuration parameter: | equipment-* | memoryPoolNumberOfPages | int | | Number of pages to be created for this equipment, taken from the chosen memory bank. The bank should have enough free space to accomodate (memoryPoolNumberOfPages + 1) * memoryPoolPageSize bytes. |
  memoryPoolPageSize = (int)ReadoutUtils::getNumberOfBytesFromString(cfgMemoryPoolPageSize.c_str());
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memoryPoolNumberOfPages", memoryPoolNumberOfPages);
  if (cfgOutputFifoSize == -1) {
    cfgOutputFifoSize = memoryPoolNumberOfPages;
  }

  // disable output?
  // configuration parameter: | equipment-* | disableOutput | int | 0 | If non-zero, data generated by this equipment is discarded immediately and is not pushed to output fifo of readout thread. Used for testing. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".disableOutput", disableOutput);

  // memory alignment
  // configuration parameter: | equipment-* | firstPageOffset | bytes | | Offset of the first page, in bytes from the beginning of the memory pool. If not set (recommended), will start at memoryPoolPageSize (one free page is kept before the first usable page for readout internal use). |
  std::string cfgStringFirstPageOffset = "0";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".firstPageOffset", cfgStringFirstPageOffset);
  size_t cfgFirstPageOffset = (size_t)ReadoutUtils::getNumberOfBytesFromString(cfgStringFirstPageOffset.c_str());
  // configuration parameter: | equipment-* | blockAlign | bytes | 2M | Alignment of the beginning of the big memory block from which the pool is created. Pool will start at a multiple of this value. Each page will then begin at a multiple of memoryPoolPageSize from the beginning of big block. |
  std::string cfgStringBlockAlign = "2M";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".blockAlign", cfgStringBlockAlign);
  size_t cfgBlockAlign = (size_t)ReadoutUtils::getNumberOfBytesFromString(cfgStringBlockAlign.c_str());

  // output periodic statistics on console
  // configuration parameter: | equipment-* | consoleStatsUpdateTime | double | 0 | If set, number of seconds between printing statistics on console. |
  cfg.getOptionalValue<double>(cfgEntryPoint + ".consoleStatsUpdateTime", cfgConsoleStatsUpdateTime);

  // configuration parameter: | equipment-* | stopOnError | int | 0 | If 1, readout will stop automatically on equipment error. |
  int cfgStopOnError = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".stopOnError", cfgStopOnError);
  if (cfgStopOnError) {
    this->stopOnError = 1;
  }
  // configuration parameter: | equipment-* | debugFirstPages | int | 0 | If set, print debug information for first (given number of) data pages readout. |
  int cfgDebugFirstPages = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".debugFirstPages", cfgDebugFirstPages);
  if (cfgDebugFirstPages >= 0) {
    this->debugFirstPages = cfgDebugFirstPages;
  }

  // get TF rate from toplevel config
  cfgTfRateLimit = 0;
  cfg.getOptionalValue<double>("readout.tfRateLimit", cfgTfRateLimit);

  // get TF disable flag from toplevel config
  cfgDisableTimeframes = 0;
  cfg.getOptionalValue<int>("readout.disableTimeframes", cfgDisableTimeframes);

  // get superpage debug settings
  // configuration parameter: | equipment-* | saveErrorPagesMax | int | 0 | If set, pages found with data error are saved to disk up to given maximum. |
  cfgSaveErrorPagesMax = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".saveErrorPagesMax", cfgSaveErrorPagesMax);
  // configuration parameter: | equipment-* | saveErrorPagesPath | string |  | Path where to save data pages with errors (when feature enabled). |
  cfgSaveErrorPagesPath = "";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".saveErrorPagesPath", cfgSaveErrorPagesPath);
  // configuration parameter: | equipment-* | dataPagesLogPath | string |  | Path where to save a summary of each data pages generated by equipment. |
  cfgDataPagesLogPath = "";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".dataPagesLogPath", cfgDataPagesLogPath);
  
  // log config summary
  theLog.log(LogInfoDevel_(3002), "Equipment %s: from config [%s], id=%d, max rate=%lf Hz, idleSleepTime=%d us, outputFifoSize=%d", name.c_str(), cfgEntryPoint.c_str(), (int)cfgEquipmentId, readoutRate, cfgIdleSleepTime, cfgOutputFifoSize);
  theLog.log(LogInfoDevel_(3008), "Equipment %s: requesting memory pool %d pages x %d bytes from bank '%s', block aligned @ 0x%X, 1st page offset @ 0x%X", name.c_str(), (int)memoryPoolNumberOfPages, (int)memoryPoolPageSize, memoryBankName.c_str(), (int)cfgBlockAlign, (int)cfgFirstPageOffset);
  if (disableOutput) {
    theLog.log(LogWarningDevel_(3002), "Equipment %s: output DISABLED ! Data will be readout and dropped immediately", name.c_str());
  }

  // RDH-related extra configuration parameters
  // configuration parameter: | equipment-* | rdhCheckEnabled | int | 0 | If set, data pages are parsed and RDH headers checked. Errors are reported in logs. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhCheckEnabled", cfgRdhCheckEnabled);
  // configuration parameter: | equipment-* | rdhDumpEnabled | int | 0 | If set, data pages are parsed and RDH headers summary printed on console. Setting a negative number will print only the first N pages.|
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhDumpEnabled", cfgRdhDumpEnabled);
  // configuration parameter: | equipment-* | rdhDumpErrorEnabled | int | 1 | If set, a log message is printed for each RDH header error found.|
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhDumpErrorEnabled", cfgRdhDumpErrorEnabled);
  // configuration parameter: | equipment-* | rdhDumpWarningEnabled | int | 1 | If set, a log message is printed for each RDH header warning found.|
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhDumpWarningEnabled", cfgRdhDumpWarningEnabled);
  // configuration parameter: | equipment-* | rdhUseFirstInPageEnabled | int | 0 or 1 | If set, the first RDH in each data page is used to populate readout headers (e.g. linkId). Default is 1 for  equipments generating data with RDH, 0 otherwsise. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhUseFirstInPageEnabled", cfgRdhUseFirstInPageEnabled);
  // configuration parameter: | equipment-* | rdhDumpFirstInPageEnabled | int | 0 | If set, the first RDH in each data page is logged. Setting a negative number will printit only for the first N pages. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhDumpFirstInPageEnabled", cfgRdhDumpFirstInPageEnabled);
  // configuration parameter: | equipment-* | rdhCheckFirstOrbit | int | 1 | If set, it is checked that the first orbit of all equipments is the same. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhCheckFirstOrbit", cfgRdhCheckFirstOrbit);
  // configuration parameter: | equipment-* | rdhCheckDetectorField | int | 0 | If set, the detector field is checked and changes reported. |
  cfg.getOptionalValue<int>(cfgEntryPoint + ".rdhCheckDetectorField", cfgRdhCheckDetectorField);  
  theLog.log(LogInfoDevel_(3002), "RDH settings: rdhCheckEnabled=%d rdhDumpEnabled=%d rdhDumpErrorEnabled=%d rdhDumpWarningEnabled=%d rdhUseFirstInPageEnabled=%d rdhCheckFirstOrbit=%d rdhCheckDetectorField=%d", cfgRdhCheckEnabled, cfgRdhDumpEnabled, cfgRdhDumpErrorEnabled, cfgRdhDumpWarningEnabled, cfgRdhUseFirstInPageEnabled, cfgRdhCheckFirstOrbit, cfgRdhCheckDetectorField);

  if (!cfgDisableTimeframes) {
    // configuration parameter: | equipment-* | TFperiod | int | 128 | Duration of a timeframe, in number of LHC orbits. |
    int cfgTFperiod = 128;
    cfg.getOptionalValue<int>(cfgEntryPoint + ".TFperiod", cfgTFperiod);
    timeframePeriodOrbits = cfgTFperiod;

    if (!cfgRdhUseFirstInPageEnabled) {
      usingSoftwareClock = true; // if RDH disabled, use internal clock for TF id
    }
    theLog.log(LogInfoDevel_(3002), "Timeframe length = %d orbits", (int)timeframePeriodOrbits);
    if (usingSoftwareClock) {
      timeframeRate = LHCOrbitRate * 1.0 / timeframePeriodOrbits; // timeframe rate, in Hz
      theLog.log(LogInfoDevel_(3002), "Timeframe IDs generated by software, %.2lf Hz", timeframeRate);
    } else {
      theLog.log(LogInfoDevel_(3002), "Timeframe IDs generated from RDH trigger counters");
    }
  }
  
  // init stats
  equipmentStats.resize(EquipmentStatsIndexes::maxIndex);
  equipmentStatsLast.resize(EquipmentStatsIndexes::maxIndex);

  // init debug file
  if (cfgDataPagesLogPath.length()) {
    theLog.log(LogInfoDevel_(3002), "Equipment %s: data pages summary will be logged to %s", name.c_str(), cfgDataPagesLogPath.c_str());
    fpDataPagesLog = fopen(cfgDataPagesLogPath.c_str(),"w");
    if (fpDataPagesLog == nullptr) {
      theLog.log(LogWarningDevel_(3232), "Failed to create log file");
    }
  }

  // creation of memory pool for data pages
  // todo: also allocate pool of DataBlockContainers? at the same time? reserve space at start of pages?
  if ((memoryPoolPageSize <= 0) || (memoryPoolNumberOfPages <= 0)) {
    theLog.log(LogErrorSupport_(3103), "Equipment %s: wrong memory pool settings", name.c_str());
    throw __LINE__;
  }
  pageSpaceReserved = sizeof(DataBlock); // reserve some data at beginning of each page for header,
                                         // keep beginning of payload aligned as requested in config
  size_t firstPageOffset = 0;            // alignment of 1st page of memory pool
  if (pageSpaceReserved) {
    // auto-align
    firstPageOffset = memoryPoolPageSize - pageSpaceReserved;
  }
  if (cfgFirstPageOffset) {
    firstPageOffset = cfgFirstPageOffset - pageSpaceReserved;
  }
  theLog.log(LogInfoDevel_(3008), "pageSpaceReserved = %d, aligning 1st page @ 0x%X", (int)pageSpaceReserved, (int)firstPageOffset);
  mp = nullptr;
  try {
    mp = theMemoryBankManager.getPagedPool(memoryPoolPageSize, memoryPoolNumberOfPages, memoryBankName, firstPageOffset, cfgBlockAlign);
  } catch (...) {
  }
  if (mp == nullptr) {
    theLog.log(LogErrorSupport_(3230), "Failed to create pool of memory pages");
    throw __LINE__;
  } else {
    mp -> setWarningCallback(std::bind(&ReadoutEquipment::mplog, this, std::placeholders::_1));
  }
  // todo: move page align to MemoryPool class
  assert(pageSpaceReserved == mp->getPageSize() - mp->getDataBlockMaxSize());

  // create output fifo
  dataOut = std::make_shared<AliceO2::Common::Fifo<DataBlockContainerReference>>(cfgOutputFifoSize);
  if (dataOut == nullptr) {
    throw __LINE__;
  }

  // create thread
  readoutThread = std::make_unique<Thread>(ReadoutEquipment::threadCallback, this, name, cfgIdleSleepTime);
  if (readoutThread == nullptr) {
    throw __LINE__;
  }
}

const std::string& ReadoutEquipment::getName() { return name; }

void ReadoutEquipment::start()
{
  // reset counters
  for (int i = 0; i < (int)EquipmentStatsIndexes::maxIndex; i++) {
    equipmentStats[i].reset();
    equipmentStatsLast[i] = 0;
  }
  equipmentLinksUsed.reset();
  equipmentLinksData.resize(RdhMaxLinkId + 1);
  equipmentLinksData.clear();
  isError = 0;
  currentBlockId = 0;
  isDataOn = false;
  saveErrorPagesCount = 0;
  
  // reset equipment counters
  ReadoutEquipment::initCounters();
  this->initCounters();

  // reset block rate clock
  if (readoutRate > 0) {
    clk.reset(1000000.0 / readoutRate);
  }
  clk0.reset();

  // reset TF rate clock
  TFregulator.init(cfgTfRateLimit);
  throttlePendingBlock = nullptr;
  
  // reset stats timer
  consoleStatsTimer.reset(cfgConsoleStatsUpdateTime * 1000000);

  readoutThread->start();
}

void ReadoutEquipment::stop()
{

  // just in case this was not done yet
  isDataOn = false;

  double runningTime = clk0.getTime();
  readoutThread->stop();
  // printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocksOut,clk0.getTimer(),nBlocksOut/clk0.getTime());
  readoutThread->join();

  this->finalCounters();
  ReadoutEquipment::finalCounters();

  // cleanup
  throttlePendingBlock = nullptr;

  for (int i = 0; i < (int)EquipmentStatsIndexes::maxIndex; i++) {
    if (equipmentStats[i].getCount()) {
      theLog.log(LogInfoDevel_(3003), "%s.%s = %llu  (avg=%.2lf  min=%llu  max=%llu  count=%llu)", name.c_str(), EquipmentStatsNames[i], (unsigned long long)equipmentStats[i].get(), equipmentStats[i].getAverage(), (unsigned long long)equipmentStats[i].getMinimum(), (unsigned long long)equipmentStats[i].getMaximum(), (unsigned long long)equipmentStats[i].getCount());
    } else {
      theLog.log(LogInfoDevel_(3003), "%s.%s = %llu", name.c_str(), EquipmentStatsNames[i], (unsigned long long)equipmentStats[i].get());
    }
  }

  theLog.log(LogInfoDevel_(3003), "Average pages pushed per iteration: %.1f", equipmentStats[EquipmentStatsIndexes::nBlocksOut].get() * 1.0 / (equipmentStats[EquipmentStatsIndexes::nLoop].get() - equipmentStats[EquipmentStatsIndexes::nIdle].get()));
  theLog.log(LogInfoDevel_(3003), "Average fifoready occupancy: %.1f", equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].get() * 1.0 / (equipmentStats[EquipmentStatsIndexes::nLoop].get() - equipmentStats[EquipmentStatsIndexes::nIdle].get()));
  theLog.log(LogInfoDevel_(3003), "Average data throughput: %s", ReadoutUtils::NumberOfBytesToString(equipmentStats[EquipmentStatsIndexes::nBytesOut].get() / runningTime, "B/s").c_str());
  theLog.log(LogInfoDevel_(3003), "Links used: %s", equipmentLinksUsed.to_string().c_str());

  std::string perLinkStats;
  for (unsigned int i = 0; i<= RdhMaxLinkId; i++) {
    if (equipmentLinksUsed[i]) {
      perLinkStats += "[" + std::to_string(i) + "]=" + NumberOfBytesToString(equipmentLinksData[i], "B", 1024) + " ";
    }
  }
  theLog.log(LogInfoDevel_(3003), "Links data received: %s", perLinkStats.c_str());
}

ReadoutEquipment::~ReadoutEquipment()
{
  readoutThread = nullptr;
  dataOut->clear();

  if (mp != nullptr) {
    theLog.log(LogInfoDevel_(3003), "Equipment %s - memory pool statistics ... %s", name.c_str(), mp->getStats().c_str());
  }

  // check if mempool still referenced
  if (!mp.unique()) {
    theLog.log(LogInfoDevel_(3008), "Equipment %s :  mempool still has %d references\n", name.c_str(), (int)mp.use_count());
  }

  if (fpDataPagesLog != nullptr) {
    fclose(fpDataPagesLog);
    fpDataPagesLog = nullptr;
  }
}

DataBlockContainerReference ReadoutEquipment::getBlock()
{
  DataBlockContainerReference b = nullptr;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult ReadoutEquipment::threadCallback(void* arg)
{
  ReadoutEquipment* ptr = static_cast<ReadoutEquipment*>(arg);

  // flag to identify if something was done in this iteration
  bool isActive = false;

  // in software clock mode, set timeframe id based on current timestamp
  if (ptr->usingSoftwareClock) {
    if (ptr->timeframeClock.isTimeout()) {
      ptr->currentTimeframe++;
      ptr->timeframeClock.increment();
    }
  }

  for (;;) {
    ptr->equipmentStats[EquipmentStatsIndexes::nLoop].increment();

    // max number of blocks to read in this iteration.
    // this is a finite value to ensure all readout steps are done regularly.
    int maxBlocksToRead = 1024;

    // check throughput
    if (ptr->readoutRate > 0) {
      uint64_t nBlocksOut = ptr->equipmentStats[(int)EquipmentStatsIndexes::nBlocksOut].get(); // number of blocks we have already readout until now
      maxBlocksToRead = ptr->readoutRate * ptr->clk0.getTime() - nBlocksOut;
      if ((!ptr->clk.isTimeout()) && (nBlocksOut != 0) && (maxBlocksToRead <= 0)) {
        // target block rate exceeded, wait a bit
        ptr->equipmentStats[EquipmentStatsIndexes::nThrottle].increment();
        break;
      }
    }

    // check status of output FIFO
    ptr->equipmentStats[EquipmentStatsIndexes::fifoOccupancyOutBlocks].set(ptr->dataOut->getNumberOfUsedSlots());

    // check status of memory pool
    {
      size_t nPagesTotal = 0, nPagesFree = 0, nPagesUsed = 0;
      if (ptr->getMemoryUsage(nPagesFree, nPagesTotal) == 0) {
        nPagesUsed = nPagesTotal - nPagesFree;
      }
      ptr->equipmentStats[EquipmentStatsIndexes::nPagesUsed].set(nPagesUsed);
      ptr->equipmentStats[EquipmentStatsIndexes::nPagesFree].set(nPagesFree);
    }

    // try to get new blocks
    int nPushedOut = 0;
    for (int i = 0; i < maxBlocksToRead; i++) {

      // check output FIFO status so that we are sure we can push next block, if any
      if (ptr->dataOut->isFull()) {
        ptr->equipmentStats[EquipmentStatsIndexes::nOutputFull].increment();
        break;
      }

      // get next block
      DataBlockContainerReference nextBlock = nullptr;
      if (ptr->throttlePendingBlock != nullptr) {      
        nextBlock = std::move(ptr->throttlePendingBlock);
      } else {
	try {
          nextBlock = ptr->getNextBlock();
	} catch (...) {
          theLog.log(LogWarningDevel_(3230), "getNextBlock() exception");
          break;
	}

	if (nextBlock == nullptr) {
          break;
	}

	// handle RDH-formatted data
	if (ptr->cfgRdhUseFirstInPageEnabled) {
          ptr->processRdh(nextBlock);
	}
	
	// tag data with equipment Id, if set (will overwrite field if was already set by equipment)
	if (ptr->id != undefinedEquipmentId) {
          nextBlock->getData()->header.equipmentId = ptr->id;
	}

	// tag data with block id
	ptr->currentBlockId++; // don't start from 0
	nextBlock->getData()->header.blockId = ptr->currentBlockId;

	if (ptr->cfgDisableTimeframes) {
	  // disable TF id
          nextBlock->getData()->header.timeframeId = undefinedTimeframeId;
	} else {
	  // tag data with (dummy) timeframeid, if none set
	  if (nextBlock->getData()->header.timeframeId == undefinedTimeframeId) {
            nextBlock->getData()->header.timeframeId = ptr->getCurrentTimeframe();
	  }
	}

	// tag data with run number
	nextBlock->getData()->header.runNumber = occRunNumber;
      }
      
      // check TF id of new block
      uint64_t tfId = nextBlock->getData()->header.timeframeId;
      if (tfId > ptr->lastTimeframe) {
        // data from all links are not necessarily synchronized:
	// at a given point in time the tfIds might be mixed between different links, some beeing still sending data for previous TF
        // tfId != ptr->lastTimeframe instead of > is too strict, as there could be some (small) jumps back (usually lastTimeframe-1)
	// the data aggregator buffer will reorder them later when needed

	// regulate TF rate if needed
	if (!ptr->TFregulator.next()) {
          ptr->throttlePendingBlock = std::move(nextBlock); // keep block with new TF for later
	  isActive = false; // ask for delay before retry
	  break;
	}

        static InfoLogger::AutoMuteToken logTFdiscontinuityToken(LogWarningSupport_(3004), 10, 60);

	ptr->statsNumberOfTimeframes++;
	// detect gaps in TF id continuity
	if (tfId != ptr->lastTimeframe + 1) {
	  if (ptr->cfgRdhDumpWarningEnabled) {
            theLog.log(logTFdiscontinuityToken, "Non-contiguous timeframe IDs %llu ... %llu", (unsigned long long)ptr->lastTimeframe, (unsigned long long)tfId);
	  }
	}
	ptr->lastTimeframe = tfId;
      }

      // update rate-limit clock
      if (ptr->readoutRate > 0) {
        ptr->clk.increment();
      }

      // update stats
      nPushedOut++;
      ptr->equipmentStats[EquipmentStatsIndexes::nBytesOut].increment(nextBlock->getData()->header.dataSize);
      gReadoutStats.counters.bytesReadout += nextBlock->getData()->header.dataSize;
      gReadoutStats.counters.notify++;
      isActive = true;

      // print block debug info
      DataBlockHeader* h = &(nextBlock->getData()->header);
      if (ptr->debugFirstPages > 0) {
        theLog.log(LogDebugDevel_(3009), "Equipment %s (%d) page %" PRIu64 " link %d tf %" PRIu64 " size %d", ptr->name.c_str(), h->equipmentId, h->blockId, h->linkId, h->timeframeId, h->dataSize);
        ptr->debugFirstPages--;
      }
      if (ptr->fpDataPagesLog != nullptr) {
        // log file format: timestamp(microsec) eqId linkId tfId size
        fprintf(ptr->fpDataPagesLog, "%" PRIu64 "\t%d\t%" PRIu64 "\t%d\t%" PRIu64 "\t%d\n",
	   (uint64_t)(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())).count(),
	   h->equipmentId, h->blockId, h->linkId, h->timeframeId, h->dataSize
	);
      }

      if (!ptr->disableOutput) {
        // push new page to output fifo
        ptr->dataOut->push(nextBlock);
      }
    }
    ptr->equipmentStats[EquipmentStatsIndexes::nBlocksOut].increment(nPushedOut);

    // prepare next blocks
    if (ptr->isDataOn) {
      Thread::CallbackResult statusPrepare = ptr->prepareBlocks();
      switch (statusPrepare) {
        case (Thread::CallbackResult::Ok):
          isActive = true;
          break;
        case (Thread::CallbackResult::Idle):
          break;
        default:
          // this is an abnormal situation, return corresponding status
          return statusPrepare;
      }
    }

    // consider inactive if we have not pushed much compared to free space in output fifo
    // todo: instead, have dynamic 'inactive sleep time' as function of actual outgoing page rate to optimize polling interval
    if (nPushedOut < ptr->dataOut->getNumberOfFreeSlots() / 4) {
      // disabled, should not depend on output fifo size
      // isActive=0;
    }

    // todo: add SLICER to aggregate together time-range data
    // todo: get other FIFO status

    // print statistics on console, if configured
    if (ptr->cfgConsoleStatsUpdateTime > 0) {
      if (ptr->consoleStatsTimer.isTimeout()) {
        for (int i = 0; i < (int)EquipmentStatsIndexes::maxIndex; i++) {
          CounterValue vNew = ptr->equipmentStats[i].getCount();
          CounterValue vDiff = vNew - ptr->equipmentStatsLast[i];
          ptr->equipmentStatsLast[i] = vNew;
          theLog.log(LogInfoDevel_(3003), "%s.%s : diff=%llu total=%llu", ptr->name.c_str(), ptr->EquipmentStatsNames[i], (unsigned long long)vDiff, (unsigned long long)vNew);
        }
        ptr->consoleStatsTimer.increment();
      }
    }

    break;
  }

  if (!isActive) {
    ptr->equipmentStats[EquipmentStatsIndexes::nIdle].increment();
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}

void ReadoutEquipment::setDataOn() { isDataOn = true; }

void ReadoutEquipment::setDataOff() { isDataOn = false; }

int ReadoutEquipment::getMemoryUsage(size_t& numberOfPagesAvailable, size_t& numberOfPagesInPool)
{
  numberOfPagesAvailable = 0;
  numberOfPagesInPool = 0;
  if (mp == nullptr) {
    return -1;
  }
  numberOfPagesInPool = mp->getTotalNumberOfPages();
  numberOfPagesAvailable = mp->getNumberOfPagesAvailable();
  return 0;
}

void ReadoutEquipment::initCounters()
{

  statsRdhCheckOk = 0;
  statsRdhCheckErr = 0;
  statsRdhCheckStreamErr = 0;

  statsNumberOfTimeframes = 0;

  // reset timeframe clock
  currentTimeframe = undefinedTimeframeId;
  lastTimeframe = undefinedTimeframeId;
  firstTimeframeHbOrbitBegin = undefinedOrbit;
  isDefinedFirstTimeframeHbOrbitBegin = 0;
  if (usingSoftwareClock) {
    timeframeClock.reset(1000000 / timeframeRate);
    currentTimeframe = 1;
  }

  isDefinedLastDetectorField = 0;
  lastDetectorField = 0;
};

void ReadoutEquipment::finalCounters()
{
  if (cfgRdhCheckEnabled) {
    theLog.log(LogInfoDevel_(3003), "Equipment %s : %llu timeframes, RDH checks %llu ok, %llu errors, %llu stream inconsistencies", name.c_str(), statsNumberOfTimeframes, statsRdhCheckOk, statsRdhCheckErr, statsRdhCheckStreamErr);
  }
};

uint64_t ReadoutEquipment::getTimeframeFromOrbit(uint32_t hbOrbit)
{
  if (!isDefinedFirstTimeframeHbOrbitBegin) {
    firstTimeframeHbOrbitBegin = hbOrbit;
    isDefinedFirstTimeframeHbOrbitBegin = 1;
    bool isOk = true;
    gReadoutStats.mutex.lock();
    if (gReadoutStats.counters.firstOrbit == undefinedOrbit) {
      gReadoutStats.counters.firstOrbit = firstTimeframeHbOrbitBegin;
      gReadoutStats.counters.notify++;
    } else if (gReadoutStats.counters.firstOrbit != firstTimeframeHbOrbitBegin) {
      isOk = false;
    }
    gReadoutStats.mutex.unlock();
    theLog.log(LogInfoDevel_(3011), "Equipment %s : first HB orbit = %X", name.c_str(), (unsigned int)firstTimeframeHbOrbitBegin);
    if (!isOk) {
      if (cfgRdhCheckFirstOrbit) {
        theLog.log(LogErrorDevel_(3241), "Equipment %s : first HB orbit is different from other equipments", name.c_str());
      }
    }
  }
  uint64_t tfId = 1 + (hbOrbit - firstTimeframeHbOrbitBegin) / getTimeframePeriodOrbits();
  return tfId;
}

void ReadoutEquipment::getTimeframeOrbitRange(uint64_t tfId, uint32_t& hbOrbitMin, uint32_t& hbOrbitMax)
{
  hbOrbitMin = undefinedOrbit;
  hbOrbitMax = undefinedOrbit;
  if (tfId == undefinedTimeframeId)
    return;
  if (!isDefinedFirstTimeframeHbOrbitBegin)
    return;
  hbOrbitMin = firstTimeframeHbOrbitBegin + (tfId - 1) * getTimeframePeriodOrbits();
  hbOrbitMax = hbOrbitMin + getTimeframePeriodOrbits() - 1;
}

uint64_t ReadoutEquipment::getCurrentTimeframe() { return currentTimeframe; }

int ReadoutEquipment::tagDatablockFromRdh(RdhHandle& h, DataBlockHeader& bh)
{

  uint64_t tfId = undefinedTimeframeId;
  uint8_t systemId = undefinedSystemId;
  uint16_t feeId = undefinedFeeId;
  uint16_t equipmentId = undefinedEquipmentId;
  uint8_t linkId = undefinedLinkId;
  uint32_t hbOrbit = undefinedOrbit;
  bool isError = 0;

  // check that it is a correct RDH
  std::string errorDescription;
  if (h.validateRdh(errorDescription) != 0) {
    theLog.log(LogWarningSupport_(3004), "First RDH in page is wrong: %s", errorDescription.c_str());
    isError = 1;
  } else {
    // timeframe ID
    hbOrbit = h.getHbOrbit() + bh.orbitOffset;
    tfId = getTimeframeFromOrbit(hbOrbit);
    // printf("orbit %X + offset %X = %X -> TFid %d\n",(int)h.getHbOrbit(), (int)bh.orbitOffset, (int)hbOrbit, (int)tfId);

    // system ID
    systemId = h.getSystemId();

    // fee ID - may not be valid for whole page
    feeId = h.getFeeId();

    // equipmentId - computed from CRU id + end-point
    equipmentId = (uint16_t)(h.getCruId() * 10 + h.getEndPointId());

    // discard value from CRU if this is the default one
    if (equipmentId == 0) {
      equipmentId = undefinedEquipmentId;
    }

    // linkId
    linkId = h.getLinkId();
  }

  bh.timeframeId = tfId;
  bh.systemId = systemId;
  bh.feeId = feeId;
  bh.equipmentId = equipmentId;
  bh.linkId = linkId;
  getTimeframeOrbitRange(tfId, bh.timeframeOrbitFirst, bh.timeframeOrbitLast);
  bh.timeframeOrbitFirst -= bh.orbitOffset;
  bh.timeframeOrbitLast -= bh.orbitOffset;
  // printf("TF %d eq %d link %d : orbits %X - %X\n", (int)bh.timeframeId, (int)bh.equipmentId, (int)bh.linkId, (int)bh.timeframeOrbitFirst, (int)bh.timeframeOrbitLast);
  return isError;
}

int ReadoutEquipment::processRdh(DataBlockContainerReference& block)
{
  bool isPageError = 0; // flag set when some errors found
  
  DataBlockHeader& blockHeader = block->getData()->header;
  void* blockData = block->getData()->data;
  if (blockData == nullptr) {
    return -1;
  }

  // retrieve metadata from RDH, if configured to do so
  if ((cfgRdhUseFirstInPageEnabled) || (cfgRdhCheckEnabled)) {
    RdhHandle h(blockData);
    if (tagDatablockFromRdh(h, blockHeader) == 0) {
      blockHeader.isRdhFormat = 1;
    }

    if (cfgRdhDumpFirstInPageEnabled) {
      theLog.log(LogInfoDevel_(3011),"Equipment %s: first RDH in page %lu",
        name.c_str(), (unsigned long) currentBlockId + 1);
      theLog.log(LogInfoDevel_(3011),"  Orbit 0x%08X BC 0x%08X Type 0x%08X" ,
	h.getTriggerOrbit(), h.getTriggerBC(), h.getTriggerType());
      theLog.log(LogInfoDevel_(3011),"  ROC %d.%d Link %d System %d FEE 0x%04X DetField 0x%08X",
	h.getCruId(), h.getEndPointId(), h.getLinkId(), h.getSystemId(), h.getFeeId(), h.getDetectorField());
      theLog.log(LogInfoDevel_(3011),"  RDH: %s", h.toHexaString().c_str());
      cfgRdhDumpFirstInPageEnabled++;
    }

    // update links statistics
    if (h.getLinkId() <= RdhMaxLinkId) {
      equipmentLinksUsed[h.getLinkId()] = 1;
      equipmentLinksData[h.getLinkId()] += blockHeader.dataSize;
    }


    // detect changes in detector bits field
    if (cfgRdhCheckDetectorField) {
      if (isDefinedLastDetectorField) {
	if (h.getDetectorField() != lastDetectorField) {
          theLog.log(LogInfoDevel_(3011), "Equipment %s: change in detector field detected: 0x%X -> 0x%X", name.c_str(), (int)lastDetectorField, (int)h.getDetectorField());
	}
      }
      lastDetectorField = h.getDetectorField();
      isDefinedLastDetectorField = 1;
    }
  }

  // Dump RDH if configured to do so
  if (cfgRdhDumpEnabled) {
    RdhBlockHandle b(blockData, blockHeader.dataSize);
    if (b.printSummary()) {
      printf("errors detected, suspending RDH dump\n");
      cfgRdhDumpEnabled = 0;
    } else {
      cfgRdhDumpEnabled++; // if value positive, it continues... but negative, it stops on zero, to limit number of dumps
    }
  }

  // validate RDH structure, if configured to do so
  if (cfgRdhCheckEnabled) {
    std::string errorDescription;
    size_t blockSize = blockHeader.dataSize;
    uint8_t* baseAddress = (uint8_t*)(blockData);
    int rdhIndexInPage = 0;
    int linkId = undefinedLinkId;

    static InfoLogger::AutoMuteToken logRdhErrorsToken(LogWarningSupport_(3004), 30, 5);

    for (size_t pageOffset = 0; pageOffset < blockSize;) {
      RdhHandle h(baseAddress + pageOffset);
      rdhIndexInPage++;

      // printf("RDH #%d @ 0x%X : next block @ +%d bytes\n",rdhIndexInPage,(unsigned int)pageOffset,h.getOffsetNextPacket());

      if (h.validateRdh(errorDescription)) {
        if ((cfgRdhDumpEnabled) || (cfgRdhDumpErrorEnabled)) {
          for (int i = 0; i < 16; i++) {
            printf("%08X ", (int)(((uint32_t*)baseAddress)[i]));
          }
          printf("\n");
          printf("Page 0x%p + %ld\n%s", (void*)baseAddress, pageOffset, errorDescription.c_str());
          h.dumpRdh(pageOffset, 1);
          errorDescription.clear();
        }
        statsRdhCheckErr++;
	isPageError = 1;
        theLog.log(logRdhErrorsToken, "Equipment %d RDH #%d @ 0x%X : invalid RDH: %s", id, rdhIndexInPage, (unsigned int)pageOffset, errorDescription.c_str());
        // stop on first RDH error (should distinguich valid/invalid block length)
        break;
      } else {
        statsRdhCheckOk++;

        if (cfgRdhDumpEnabled) {
          h.dumpRdh(pageOffset, 1);
          for (int i = 0; i < 16; i++) {
            printf("%08X ", (int)(((uint32_t*)baseAddress + pageOffset)[i]));
          }
          printf("\n");
        }
      }

      // linkId should be same everywhere in page
      if (pageOffset == 0) {
        linkId = h.getLinkId(); // keep link of 1st RDH
      }
      if (linkId != h.getLinkId()) {
        if (cfgRdhDumpWarningEnabled) {
          theLog.log(logRdhErrorsToken, "Equipment %d RDH #%d @ 0x%X : inconsistent link ids: %d != %d", id, rdhIndexInPage, (unsigned int)pageOffset, linkId, h.getLinkId());
          isPageError = 1;
        }
        statsRdhCheckStreamErr++;
        break; // stop checking this page
      }

      // check no timeframe overlap in page
      if (!cfgDisableTimeframes) {
	if (((blockHeader.timeframeOrbitFirst < blockHeader.timeframeOrbitLast) && ((h.getTriggerOrbit() < blockHeader.timeframeOrbitFirst) || (h.getTriggerOrbit() > blockHeader.timeframeOrbitLast))) || ((blockHeader.timeframeOrbitFirst > blockHeader.timeframeOrbitLast) && ((h.getTriggerOrbit() < blockHeader.timeframeOrbitFirst) && (h.getTriggerOrbit() > blockHeader.timeframeOrbitLast)))) {
          if (cfgRdhDumpErrorEnabled) {
            theLog.log(logRdhErrorsToken, "Equipment %d Link %d RDH %d @ 0x%X : TimeFrame ID change in page not allowed : orbit 0x%08X not in range [0x%08X,0x%08X]", id, (int)blockHeader.linkId, rdhIndexInPage, (unsigned int)pageOffset, (int)h.getTriggerOrbit(), (int)blockHeader.timeframeOrbitFirst, (int)blockHeader.timeframeOrbitLast);
            isPageError = 1;
          }
          statsRdhCheckStreamErr++;
          break; // stop checking this page
	}
      }
      
      if ((isDefinedLastDetectorField)&&(pageOffset)) {
      if (h.getDetectorField() != lastDetectorField) {
        theLog.log(LogWarningDevel_(3011), "Equipment %s: change in detector field detected: 0x%X -> 0x%X", name.c_str(), (int)lastDetectorField, (int)h.getDetectorField());
      }
    }
      /*
      // check packetCounter is contiguous
      if (cfgRdhCheckPacketCounterContiguous) {
        uint8_t newCount = h.getPacketCounter();
        // no boundary check necessary to verify linkId<=RdhMaxLinkId, this was done in validateRDH()
        if (newCount != RdhLastPacketCounter[linkId]) {
          if (newCount !=
              (uint8_t)(RdhLastPacketCounter[linkId] + (uint8_t)1)) {
            theLog.log(LogDebugTrace,
                       "RDH #%d @ 0x%X : possible packets dropped for link %d, packetCounter jump from %d to %d",
                       rdhIndexInPage, (unsigned int)pageOffset,
                       (int)linkId, (int)RdhLastPacketCounter[linkId],
                       (int)newCount);
          }
          RdhLastPacketCounter[linkId] = newCount;
        }
      }
      */

      // todo: check counter increasing all have same TF id

      uint16_t offsetNextPacket = h.getOffsetNextPacket();
      if (offsetNextPacket == 0) {
        break;
      }
      pageOffset += offsetNextPacket;
    }
  }
  
  if (isPageError) {
    if (saveErrorPagesCount < cfgSaveErrorPagesMax) {
      saveErrorPagesCount++;
      std::string fn = cfgSaveErrorPagesPath + "/readout.superpage." + std::to_string(saveErrorPagesCount) + ".raw";
      theLog.log(LogInfoSupport, "Equipment %d : saving superpage %p with errors to disk : %s (%d bytes)", id, blockData, fn.c_str(), blockHeader.dataSize);
      FILE *fp;
      bool success = 0;
      fp = fopen(fn.c_str(), "wb");
      if (fp != nullptr) {
        if (fwrite(blockData, blockHeader.dataSize, 1, fp) == 1) {
	  success = 1;
        }
        fclose(fp);
      }
      if (!success) {
        theLog.log(LogErrorSupport_(3132), "Failed to save superpage to file : %s", strerror(errno));
      }
    }
  }
  
  return 0;
}

void ReadoutEquipment::abortThread() {
  // ensure thread is stopped
  readoutThread = nullptr;
}

void ReadoutEquipment::mplog(const std::string &msg) {
  static InfoLogger::AutoMuteToken logMPToken(LogWarningSupport_(3230), 10, 60);
  theLog.log(logMPToken, "Equipment %s : %s", name.c_str(), msg.c_str());
}
