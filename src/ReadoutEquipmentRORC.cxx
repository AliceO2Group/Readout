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

#include <Common/Timer.h>
#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/Parameters.h>
#include <cstring>
#include <mutex>
#include <string>
#include <inttypes.h>

#include "RdhUtils.h"
#include "ReadoutEquipment.h"
#include "ReadoutUtils.h"
#include "readoutInfoLogger.h"

class ReadoutEquipmentRORC : public ReadoutEquipment
{

 public:
  ReadoutEquipmentRORC(ConfigFile& cfg, std::string name = "rorcReadout");
  ~ReadoutEquipmentRORC();

 private:
  Thread::CallbackResult prepareBlocks();
  DataBlockContainerReference getNextBlock();
  void setDataOn();
  void setDataOff();
  void initCounters();
  void finalCounters();

  Thread::CallbackResult populateFifoOut(); // the data readout loop function

  AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel; // channel to ROC device

  bool isInitialized = false;     // flag set to 1 when class has been successfully initialized
  bool isWaitingFirstLoop = true; // flag set until first readout loop called

  int RocFifoSize = 0; // detected size of ROC fifo (when filling it for the first time)

  int cfgCleanPageBeforeUse = 0; // flag to enable filling page with zeros before giving for writing

  int cfgFirmwareCheckEnabled = 1; // RORC lib check self-compatibility with fw
  int cfgDebugStatsEnabled = 0;    // collect and print more buffer stats

  unsigned long long statsNumberOfPages = 0;      // number of pages read out
  unsigned long long statsNumberOfPagesEmpty = 0; // number of empty pages read out
  unsigned long long statsNumberOfPagesLost = 0;  // number of pages read out but lost

  uint8_t RdhLastPacketCounter[RdhMaxLinkId + 1]; // last value of packetCounter RDH field for each link id

  size_t superPageSize = 0; // usable size of a superpage

  int32_t lastPacketDropped = 0;             // latest value of CRU packet dropped counter
  AliceO2::Common::Timer packetDroppedTimer; // a timer to set period of packet dropped checks
};

// std::mutex readoutEquipmentRORCLock;

struct ReadoutEquipmentRORCException : virtual Exception {
};

ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile& cfg, std::string name) : ReadoutEquipment(cfg, name, 1)  // this is RDH-data equipment
{

  try {

    // get parameters from configuration
    // config keys are the same as the corresponding set functions in AliceO2::roc::Parameters

    // configuration parameter: | equipment-rorc-* | cardId | string | | ID of the board to be used. Typically, a PCI bus device id. c.f. AliceO2::roc::Parameters. |
    std::string cardId = cfg.getValue<std::string>(name + ".cardId");

    // configuration parameter: | equipment-rorc-* | channelNumber | int | 0 | Channel number of the board to be used. Typically 0 for CRU, or 0-5 for CRORC. c.f. AliceO2::roc::Parameters. |
    int cfgChannelNumber = 0;
    cfg.getOptionalValue<int>(name + ".channelNumber", cfgChannelNumber);

    // configuration parameter: | equipment-rorc-* | dataSource | string | Internal | This parameter selects the data source used by ReadoutCard, c.f. AliceO2::roc::Parameters. It can be for CRU one of Fee, Ddg, Internal and for CRORC one of Fee, SIU, DIU, Internal. |
    std::string cfgDataSource = "Internal";
    cfg.getOptionalValue<std::string>(name + ".dataSource", cfgDataSource);

    // std::string cfgReadoutMode="CONTINUOUS";
    // cfg.getOptionalValue<std::string>(name + ".readoutMode", cfgReadoutMode);

    // configuration parameter: | equipment-rorc-* | cleanPageBeforeUse | int | 0 | If set, data pages are filled with zero before being given for writing by device. Slow, but usefull to readout incomplete pages (driver currently does not return correctly number of bytes written in page. |
    cfg.getOptionalValue<int>(name + ".cleanPageBeforeUse", cfgCleanPageBeforeUse);
    if (cfgCleanPageBeforeUse) {
      theLog.log(LogInfoDevel_(3002), "Superpages will be cleaned before each DMA - this may be slow!");
    }

    // configuration parameter: | equipment-rorc-* | firmwareCheckEnabled | int | 1 | If set, RORC driver checks compatibility with detected firmware. Use 0 to bypass this check (eg new fw version not yet recognized by ReadoutCard version). |
    cfg.getOptionalValue<int>(name + ".firmwareCheckEnabled", cfgFirmwareCheckEnabled);
    if (!cfgFirmwareCheckEnabled) {
      theLog.log(LogWarningSupport_(3002), "Bypassing RORC firmware compatibility check");
    }

    // configuration parameter: | equipment-rorc-* | debugStatsEnabled | int | 0 | If set, enable extra statistics about internal buffers status. (printed to stdout when stopping) |
    cfg.getOptionalValue<int>(name + ".debugStatsEnabled", cfgDebugStatsEnabled);

    // get readout memory buffer parameters
    // std::string sMemorySize=cfg.getValue<std::string>(name + ".memoryBufferSize");
    // std::string sPageSize=cfg.getValue<std::string>(name + ".memoryPageSize"); long long
    // mMemorySize=ReadoutUtils::getNumberOfBytesFromString(sMemorySize.c_str());
    // long long mPageSize=ReadoutUtils::getNumberOfBytesFromString(sPageSize.c_str());
    // std::string cfgHugePageSize="1GB";
    // cfg.getOptionalValue<std::string>(name + ".memoryHugePageSize",cfgHugePageSize);

    // unique identifier based on card ID
    std::string uid = "readout." + cardId + "." + std::to_string(cfgChannelNumber);
    // sleep((cfgChannelNumber+1)*2);  // trick to avoid all channels open at once - fail to acquire lock

    // define usable superpagesize
    superPageSize = mp->getPageSize() - pageSpaceReserved; // Keep space at beginning for DataBlock object
    superPageSize -= superPageSize % (32 * 1024);          // Must be a multiple of 32Kb for ROC
    theLog.log(LogInfoDevel_(3008), "Using superpage size %ld", superPageSize);
    if (superPageSize == 0) {
      BOOST_THROW_EXCEPTION(ReadoutEquipmentRORCException() << ErrorInfo::Message("Superpage must be at least 32kB"));
    }

    // open and configure ROC
    theLog.log(LogInfoDevel_(3010), "Opening ROC %s:%d", cardId.c_str(), cfgChannelNumber);
    AliceO2::roc::Parameters params;
    params.setCardId(AliceO2::roc::Parameters::cardIdFromString(cardId));
    params.setChannelNumber(cfgChannelNumber);
    params.setFirmwareCheckEnabled(cfgFirmwareCheckEnabled);

    // setDmaPageSize() : seems deprecated, let's not configure it

    // card data source
    params.setDataSource(AliceO2::roc::DataSource::fromString(cfgDataSource));

    // card readout mode : experimental, not needed
    // params.setReadoutMode(AliceO2::roc::ReadoutMode::fromString(cfgReadoutMode));

    /*
    theLog.log(LogDebugTrace, "Loop DMA block %p:%lu", mp->getBaseBlockAddress(), // mp->getBaseBlockSize()); 
    char *ptr=(char *)mp->getBaseBlockAddress();
    for (size_t i=0;i<mp->getBaseBlockSize();i++) { 
      ptr[i]=0;
    }
    */

    // register the memory block for DMA
    void* baseAddress = (void*)mp->getBaseBlockAddress();
    size_t blockSize = mp->getBaseBlockSize();
    theLog.log(LogInfoDevel_(3010), "Register DMA block %p:%lu", baseAddress, blockSize);
    params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory{ baseAddress, blockSize });

    // open channel with above parameters
    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);

    // retrieve card information
    std::string infoPciAddress = channel->getPciAddress().toString();
    int infoNumaNode = channel->getNumaNode();
    std::string infoSerialNumber = "unknown";
    auto v_infoSerialNumber = channel->getSerial();
    if (v_infoSerialNumber) {
      infoSerialNumber = std::to_string(v_infoSerialNumber.get());
    }
    std::string infoFirmwareVersion = channel->getFirmwareInfo().value_or("unknown");
    std::string infoCardId = channel->getCardId().value_or("unknown");
    theLog.log(LogInfoDevel_(3010), "Equipment %s : PCI %s @ NUMA node %d, serial number %s, firmware version %s, card id %s", name.c_str(), infoPciAddress.c_str(), infoNumaNode, infoSerialNumber.c_str(), infoFirmwareVersion.c_str(), infoCardId.c_str());

    // todo: log parameters ?

  } catch (const std::exception& e) {
    theLog.log(LogErrorSupport_(3240), "Exception : %s", e.what());
    theLog.log(LogErrorSupport_(3240), "%s", boost::diagnostic_information(e).c_str());
    throw; // propagate error
    return;
  }
  isInitialized = true;
}

ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {}

Thread::CallbackResult ReadoutEquipmentRORC::prepareBlocks()
{
  if (!isInitialized)
    return Thread::CallbackResult::Error;
  if (!isDataOn)
    return Thread::CallbackResult::Idle;

  int isActive = 0;

  // check status of packets dropped
  // Returns the number of dropped packets, as reported by the BAR
  if ((isWaitingFirstLoop) || (packetDroppedTimer.isTimeout())) {
    int32_t currentPacketDropped = channel->getDroppedPackets();
    if ((currentPacketDropped != lastPacketDropped) && (!isWaitingFirstLoop)) {
      int32_t newPacketDropped = (currentPacketDropped - lastPacketDropped);
      if (newPacketDropped > 0) {
        static InfoLogger::AutoMuteToken logToken(LogWarningSupport_(3235), 10, 60);
        theLog.log(logToken, "Equipment %s: CRU has dropped packets (new=%d total=%d)", name.c_str(), newPacketDropped, currentPacketDropped);
        if (stopOnError) {
          theLog.log(LogErrorSupport_(3235), "Equipment %s: some data has been lost)", name.c_str());
        }
        isError++;
      }
    }
    lastPacketDropped = currentPacketDropped;
    if (isWaitingFirstLoop) {
      packetDroppedTimer.reset(1000000); // 1 sec interval
    } else {
      packetDroppedTimer.increment();
    }
  }

  // keep track of situations where the queue is completely empty
  // this means we have not filled it fast enough (except in first loop, where it's normal it is empty)
  if (!isWaitingFirstLoop) {
    int nFreeSlots = channel->getTransferQueueAvailable();
    if (nFreeSlots >= RocFifoSize -1 ) {
      equipmentStats[EquipmentStatsIndexes::nFifoUpEmpty].increment();
    }
    equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].set(nFreeSlots);
  }

  // give free pages to the driver
  int nPushed = 0; // number of free pages pushed this iteration
  while (channel->getTransferQueueAvailable() != 0) {
    void* newPage = mp->getPage();
    if (newPage != nullptr) {
      // todo: check page is aligned as expected
      // optionnaly, cleanup page before use
      if (cfgCleanPageBeforeUse) {
        std::memset(newPage, 0, mp->getPageSize());
      }
      AliceO2::roc::Superpage superpage;
      superpage.setOffset((char*)newPage - (char*)mp->getBaseBlockAddress() + pageSpaceReserved);
      superpage.setSize(superPageSize);
      // printf("pushed page %d\n",(int)superPageSize);
      superpage.setUserData(newPage);
      if (channel->pushSuperpage(superpage)) {
        isActive = 1;
        nPushed++;
      } else {
        // push failed (typically, stopDma() has been called in the mean time)
        // release allocated page to memory pool
        mp->releasePage(newPage);
        isActive = 0;
        break;
      }
    } else {
      equipmentStats[EquipmentStatsIndexes::nMemoryLow].increment();
      isActive = 0;
      break;
    }
  }
  equipmentStats[EquipmentStatsIndexes::nPushedUp].increment(nPushed);

  // check fifo occupancy ready queue size for stats
  equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].set(channel->getReadyQueueSize());
  if (channel->getReadyQueueSize() >= RocFifoSize - 1) {
    equipmentStats[EquipmentStatsIndexes::nFifoReadyFull].increment();
  }

  // if we have not put many pages (<25%) in ROC fifo, we can wait a bit
  if (nPushed < RocFifoSize / 4) {
    isActive = 0;
  }

  // This global mutex was also used as a fix to allow reading out 2 CRORC at same time otherwise machine reboots when ACPI is not OFF
  // readoutEquipmentRORCLock.lock();

  // this is to be called periodically for driver internal business
  channel->fillSuperpages();

  // readoutEquipmentRORCLock.unlock();

  // from time to time, we may monitor temperature
  // virtual boost::optional<float> getTemperature() = 0;

  // first loop now completed
  if (isWaitingFirstLoop) {
    isWaitingFirstLoop = false;
  }

  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}

DataBlockContainerReference ReadoutEquipmentRORC::getNextBlock()
{

  DataBlockContainerReference nextBlock = nullptr;

  // ensure the initialization was fine in the main thread
  if (!isInitialized) {
    return nullptr;
  }
  // channel->fillSuperpages();

  // check for completed page
  const int maxLoop = 2000;
  for(int loop = 0; loop < maxLoop; loop++) {
    bool doLoop = 0;
    if ((channel->getReadyQueueSize() > 0)) {
      // get next page from FIFO
      auto superpage = channel->popSuperpage();
      void* mpPageAddress = (void*)(superpage.getUserData());
      if (superpage.isReady()) {
	std::shared_ptr<DataBlockContainer> d = nullptr;
	// printf ("received a page with %d bytes - isFilled=%d isREady=%d\n", (int)superpage.getReceived(),(int)superpage.isFilled(),(int)superpage.isReady());
	if (!mp->isPageValid(mpPageAddress)) {
          theLog.log(LogWarningSupport_(3008), "Got an invalid page from RORC : %p", mpPageAddress);
	} else {
          try {
            // there is some space reserved at beginning of page for a DataBlock
            d = mp->getNewDataBlockContainer(mpPageAddress);
          } catch (...) {
          }
	}
	if (d != nullptr) {
          statsNumberOfPages++;
          nextBlock = d;
          d->getData()->header.dataSize = superpage.getReceived();

          // printf("\nPage %llu\n",statsNumberOfPages);
	} else {
          // there is a ready superpage, but we are not able to keep it
          statsNumberOfPagesLost++;
	}
      } else {
	// these are leftover pages not ready, simply discard them
	statsNumberOfPagesEmpty++;
      }

      if (nextBlock == nullptr) {
	// the superpage is not used, release it
	mp->releasePage(mpPageAddress);
	// try again to get a non-empty page, without sleeping idle
	doLoop = 1;
      }
    }
    if (!doLoop) {
      break;
    }
  }

  return nextBlock;
}

std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ReadoutEquipmentRORC>(cfg, cfgEntryPoint); }

void ReadoutEquipmentRORC::setDataOn()
{
  if (isInitialized) {
    // start DMA
    theLog.log(LogInfoDevel_(3010), "Starting DMA for ROC %s", getName().c_str());
    channel->startDma();

    // get FIFO depth (it should be fully empty when starting)
    // can not be done before startDma() - would return 0
    RocFifoSize = channel->getTransferQueueAvailable();
    theLog.log(LogInfoDevel_(3010), "ROC input queue size = %d pages", RocFifoSize);
    if (RocFifoSize == 0) {
      RocFifoSize = 1;
    }
    // enable enhanced statistics
    if (cfgDebugStatsEnabled) {
      equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].enableHistogram(12, 0, RocFifoSize, 0);
      equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].enableHistogram(12, 0, RocFifoSize, 0);
    }
  }
  ReadoutEquipment::setDataOn();

  // wait confirmation that 1st loop executed before returning
  // this ensures ROC buffer populated before declaring "running"
  AliceO2::Common::Timer firstLoopTimeout;
  firstLoopTimeout.reset(cfgIdleSleepTime * 100);
  for (;;) {
    if (!isWaitingFirstLoop) {
      theLog.log(LogInfoDevel_(3010), "Buffers ready for ROC %s", getName().c_str());
      break;
    }
    if (firstLoopTimeout.isTimeout()) {
      theLog.log(LogInfoDevel_(3010), "Buffers not yet ready for ROC %s", getName().c_str());
      break;
    }
    usleep(cfgIdleSleepTime / 4);
  }
}

void ReadoutEquipmentRORC::setDataOff()
{
  // ensure we don't push pages any more
  ReadoutEquipment::setDataOff();

  // no need to wait, stopDma() immediately disables push()
  // if there would be one pending in device thread loop

  if (isInitialized) {
    theLog.log(LogInfoDevel_(3010), "Stopping DMA for ROC %s", getName().c_str());
    try {
      channel->stopDma();
    } catch (const std::exception& e) {
      theLog.log(LogErrorSupport_(3240), "Exception : %s", e.what());
      theLog.log(LogErrorSupport_(3240), "%s", boost::diagnostic_information(e).c_str());
    }
  }
}

void ReadoutEquipmentRORC::initCounters()
{
  isWaitingFirstLoop = true;
  RocFifoSize = 0;

  // reset stats
  statsNumberOfPages = 0;
  statsNumberOfPagesEmpty = 0;
  statsNumberOfPagesLost = 0;

  // reset packetCounter monitor
  for (unsigned int i = 0; i <= RdhMaxLinkId; i++) {
    RdhLastPacketCounter[i] = 0;
  }
}

void ReadoutEquipmentRORC::finalCounters()
{

  theLog.log(LogInfoDevel_(3003), "Equipment %s : %llu pages (+ %llu lost + %llu empty), %d packets dropped by CRU", name.c_str(), statsNumberOfPages, statsNumberOfPagesLost, statsNumberOfPagesEmpty, lastPacketDropped);

  if (cfgDebugStatsEnabled) {

    printf("\n*** begin debug stats ***\n");

    std::vector<double> tx;
    std::vector<CounterValue> tv;
    CounterValue ts;

    auto dumpStats = [&](bool revert) {
      ts = 0;
      for (unsigned int i = 0; i < tx.size(); i++) {
        ts += tv[i];
      }
      printf("Fifo used (%%)\tSamples count\tSamples fraction (%%)\n");
      for (unsigned int i = 0; i < tx.size(); i++) {
        double t1 = tx[i] * 100.0 / RocFifoSize;
        if (revert) {
          t1 = 100 - t1;
        }
        double tr = 0.0;
        if (ts != 0) {
          tr = tv[i] * 100.0 / ts;
        }
        if ((i == 0) || (i == tx.size() - 1)) {
          printf("%3d       \t%13" PRIu64 "\t%3.1lf\n", (int)t1, tv[i], tr);
        } else {
          double t2 = tx[i + 1] * 100.0 / RocFifoSize;
          if (revert) {
            t2 = 100 - t2;
          }
          printf("%3d - %3d     \t%13" PRIu64 "\t%3.1lf\n", (int)t1, (int)t2, tv[i], tr);
        }
      }
    };

    equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].getHisto(tx, tv);
    printf("\nRORC transfer queue\n");
    dumpStats(1);

    equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].getHisto(tx, tv);
    printf("\nRORC ready queue\n");
    dumpStats(0);

    printf("\n*** end debug stats ***\n");
  }
}

