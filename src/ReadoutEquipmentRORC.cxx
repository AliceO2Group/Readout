// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "ReadoutEquipment.h"

#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/Parameters.h>

#include <cstring>
#include <mutex>
#include <string>

#include <Common/Timer.h>

#include "RdhUtils.h"
#include "ReadoutUtils.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

class ReadoutEquipmentRORC : public ReadoutEquipment {

public:
  ReadoutEquipmentRORC(ConfigFile &cfg, std::string name = "rorcReadout");
  ~ReadoutEquipmentRORC();

private:
  Thread::CallbackResult prepareBlocks();
  DataBlockContainerReference getNextBlock();
  void setDataOn();
  void setDataOff();

  Thread::CallbackResult populateFifoOut(); // the data readout loop function

  AliceO2::roc::ChannelFactory::DmaChannelSharedPtr
      channel; // channel to ROC device

  DataBlockId currentId = 0; // current data id, kept for auto-increment

  bool isInitialized =
      false; // flag set to 1 when class has been successfully initialized
  bool isWaitingFirstLoop = true; // flag set until first readout loop called

  int RocFifoSize =
      0; // detected size of ROC fifo (when filling it for the first time)

  int cfgRdhCheckEnabled = 0;     // flag to enable RDH check at runtime
  int cfgRdhDumpEnabled = 0;      // flag to enable RDH dump at runtime
  int cfgRdhDumpErrorEnabled = 1; // flag to enable RDH error log at runtime
  int cfgRdhUseFirstInPageEnabled = 0; // flag to enable reading of first RDH in
                                       // page to populate readout headers
  int cfgRdhCheckPacketCounterContiguous =
      1; // flag to enable checking if RDH packetCounter value contiguous (done
         // link-by-link)

  int cfgCleanPageBeforeUse =
      0; // flag to enable filling page with zeros before giving for writing

  unsigned long long statsRdhCheckOk =
      0; // number of RDH structs which have passed check ok
  unsigned long long statsRdhCheckErr =
      0; // number of RDH structs which have not passed check
  unsigned long long statsRdhCheckStreamErr =
      0; // number of inconsistencies in RDH stream (e.g. ids/timing compared to
         // previous RDH)
  unsigned long long statsNumberOfPages = 0; // number of pages read out
  unsigned long long statsNumberOfTimeframes =
      0; // number of timeframes read out

  AliceO2::Common::Timer
      timeframeClock; // timeframe id should be increased at each clock cycle
  uint64_t currentTimeframe = 0; // id of current timeframe
  bool usingSoftwareClock =
      false; // if set, using internal software clock to generate timeframe id

  const unsigned int LHCBunches = 3564; // number of bunches in LHC
  const unsigned int LHCOrbitRate =
      11246; // LHC orbit rate, in Hz. 299792458 / 26659
  uint32_t timeframePeriodOrbits =
      256; // timeframe interval duration in number of LHC orbits

  uint32_t currentTimeframeHbOrbitBegin =
      0; // HbOrbit of beginning of timeframe
  uint32_t firstTimeframeHbOrbitBegin =
      0; // HbOrbit of beginning of first timeframe

  uint8_t RdhLastPacketCounter[RdhMaxLinkId + 1]; // last value of packetCounter
                                                  // RDH field for each link id

  size_t superPageSize = 0; // usable size of a superpage

  int32_t lastPacketDropped = 0; // latest value of CRU packet dropped counter
  AliceO2::Common::Timer
      packetDroppedTimer; // a timer to set period of packet dropped checks
};

// std::mutex readoutEquipmentRORCLock;

struct ReadoutEquipmentRORCException : virtual Exception {};

ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name)
    : ReadoutEquipment(cfg, name) {

  try {

    // get parameters from configuration
    // config keys are the same as the corresponding set functions in
    // AliceO2::roc::Parameters

    // configuration parameter: | equipment-rorc-* | cardId | string | | ID of
    // the board to be used. Typically, a PCI bus device id. c.f.
    // AliceO2::roc::Parameters. |
    std::string cardId = cfg.getValue<std::string>(name + ".cardId");

    // configuration parameter: | equipment-rorc-* | channelNumber | int | 0 |
    // Channel number of the board to be used. Typically 0 for CRU, or 1-6 for
    // CRORC. c.f. AliceO2::roc::Parameters. |
    int cfgChannelNumber = 0;
    cfg.getOptionalValue<int>(name + ".channelNumber", cfgChannelNumber);

    // configuration parameter: | equipment-rorc-* | dataSource | string |
    //  Internal | This parameter selects the data source used by ReadoutCard,
    // c.f. AliceO2::roc::Parameters. It can be for CRU one of Fee, Ddg,
    // Internal and for CRORC one of Fee, SIU, DIU, Internal. |
    std::string cfgDataSource = "Internal";
    cfg.getOptionalValue<std::string>(name + ".dataSource", cfgDataSource);

    // configuration parameter: | equipment-rorc-* | linkMask | string | 0-31 |
    // List of links to be enabled. For CRU, in the 0-31 range. Can be a single
    // value, a comma-separated list, a range or comma-separated list of ranges.
    // c.f. AliceO2::roc::Parameters. |
    std::string cfgLinkMask = "0-31";
    cfg.getOptionalValue<std::string>(name + ".linkMask", cfgLinkMask);

    // std::string cfgReadoutMode="CONTINUOUS";
    // cfg.getOptionalValue<std::string>(name + ".readoutMode", cfgReadoutMode);

    // configuration parameter: | equipment-rorc-* | resetLevel | string |
    // INTERNAL | Reset level of the device. Can be one of NOTHING, INTERNAL,
    // INTERNAL_DIU, INTERNAL_DIU_SIU. c.f. AliceO2::roc::Parameters. |
    std::string cfgResetLevel = "INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".resetLevel", cfgResetLevel);

    // extra configuration parameters
    // configuration parameter: | equipment-rorc-* | rdhCheckEnabled | int | 0 |
    // If set, data pages are parsed and RDH headers checked. Errors are
    // reported in logs. |
    cfg.getOptionalValue<int>(name + ".rdhCheckEnabled", cfgRdhCheckEnabled);
    // configuration parameter: | equipment-rorc-* | rdhDumpEnabled | int | 0 |
    // If set, data pages are parsed and RDH headers summary printed. Setting a
    // negative number will print only the first N RDH.|
    cfg.getOptionalValue<int>(name + ".rdhDumpEnabled", cfgRdhDumpEnabled);
    // configuration parameter: | equipment-rorc-* | rdhDumpErrorEnabled | int |
    // 1 | If set, a log message is printed for each RDH header error found.|
    cfg.getOptionalValue<int>(name + ".rdhDumpErrorEnabled",
                              cfgRdhDumpErrorEnabled);
    // configuration parameter: | equipment-rorc-* | rdhUseFirstInPageEnabled |
    // int | 0 | If set, the first RDH in each data page is used to populate
    // readout headers (e.g. linkId).|
    cfg.getOptionalValue<int>(name + ".rdhUseFirstInPageEnabled",
                              cfgRdhUseFirstInPageEnabled);

    // configuration parameter: | equipment-rorc-* | cleanPageBeforeUse | int |
    // 0 | If set, data pages are filled with zero before being given for
    // writing by device. Slow, but usefull to readout incomplete pages (driver
    // currently does not return correctly number of bytes written in page. |
    cfg.getOptionalValue<int>(name + ".cleanPageBeforeUse",
                              cfgCleanPageBeforeUse);
    if (cfgCleanPageBeforeUse) {
      theLog.log(
          "Superpages will be cleaned before each DMA - this may be slow!");
    }

    // configuration parameter: | equipment-rorc-* | TFperiod | int | 256 |
    // Duration of a timeframe, in number of LHC orbits. |
    int cfgTFperiod = 256;
    cfg.getOptionalValue<int>(name + ".TFperiod", cfgTFperiod);
    timeframePeriodOrbits = cfgTFperiod;

    /*    // get readout memory buffer parameters
        std::string sMemorySize=cfg.getValue<std::string>(name +
       ".memoryBufferSize"); std::string
       sPageSize=cfg.getValue<std::string>(name + ".memoryPageSize"); long long
       mMemorySize=ReadoutUtils::getNumberOfBytesFromString(sMemorySize.c_str());
        long long
       mPageSize=ReadoutUtils::getNumberOfBytesFromString(sPageSize.c_str());

        std::string cfgHugePageSize="1GB";
        cfg.getOptionalValue<std::string>(name +
       ".memoryHugePageSize",cfgHugePageSize);
    */
    // unique identifier based on card ID
    std::string uid =
        "readout." + cardId + "." + std::to_string(cfgChannelNumber);
    // sleep((cfgChannelNumber+1)*2);  // trick to avoid all channels open at
    // once - fail to acquire lock

    // define usable superpagesize
    superPageSize =
        mp->getPageSize() -
        pageSpaceReserved; // Keep space at beginning for DataBlock object
    superPageSize -=
        superPageSize % (32 * 1024); // Must be a multiple of 32Kb for ROC
    theLog.log("Using superpage size %ld", superPageSize);
    if (superPageSize == 0) {
      BOOST_THROW_EXCEPTION(
          ReadoutEquipmentRORCException()
          << ErrorInfo::Message("Superpage must be at least 32kB"));
    }

    // open and configure ROC
    theLog.log("Opening ROC %s:%d", cardId.c_str(), cfgChannelNumber);
    AliceO2::roc::Parameters params;
    params.setCardId(AliceO2::roc::Parameters::cardIdFromString(cardId));
    params.setChannelNumber(cfgChannelNumber);

    // setDmaPageSize() : seems deprecated, let's not configure it

    // card data source
    params.setDataSource(AliceO2::roc::DataSource::fromString(cfgDataSource));

    // card readout mode : experimental, not needed
    // params.setReadoutMode(AliceO2::roc::ReadoutMode::fromString(cfgReadoutMode));

    /*
    theLog.log("Loop DMA block %p:%lu", mp->getBaseBlockAddress(),
    mp->getBaseBlockSize()); char *ptr=(char *)mp->getBaseBlockAddress(); for
    (size_t i=0;i<mp->getBaseBlockSize();i++) { ptr[i]=0;
    }
    */

    // register the memory block for DMA
    void *baseAddress = (void *)mp->getBaseBlockAddress();
    size_t blockSize = mp->getBaseBlockSize();
    theLog.log("Register DMA block %p:%lu", baseAddress, blockSize);
    params.setBufferParameters(
        AliceO2::roc::buffer_parameters::Memory{baseAddress, blockSize});

    // define link mask
    // this is harmless for C-RORC
    params.setLinkMask(
        AliceO2::roc::Parameters::linkMaskFromString(cfgLinkMask));

    // open channel with above parameters
    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);
    channel->resetChannel(AliceO2::roc::ResetLevel::fromString(cfgResetLevel));

    // retrieve card information
    std::string infoPciAddress = channel->getPciAddress().toString();
    int infoNumaNode = channel->getNumaNode();
    std::string infoSerialNumber = "unknown";
    auto v_infoSerialNumber = channel->getSerial();
    if (v_infoSerialNumber) {
      infoSerialNumber = std::to_string(v_infoSerialNumber.get());
    }
    std::string infoFirmwareVersion =
        channel->getFirmwareInfo().value_or("unknown");
    std::string infoCardId = channel->getCardId().value_or("unknown");
    theLog.log("Equipment %s : PCI %s @ NUMA node %d, serial number %s, "
               "firmware version %s, card id %s",
               name.c_str(), infoPciAddress.c_str(), infoNumaNode,
               infoSerialNumber.c_str(), infoFirmwareVersion.c_str(),
               infoCardId.c_str());

    // todo: log parameters ?

    // reset timeframe id
    currentTimeframe = 0;
    if (!cfgRdhUseFirstInPageEnabled) {
      usingSoftwareClock =
          true; // if RDH disabled, use internal clock for TF id
    }
    theLog.log("Timeframe length = %d orbits", (int)timeframePeriodOrbits);
    if (usingSoftwareClock) {
      // reset timeframe clock
      double timeframeRate =
          LHCOrbitRate * 1.0 / timeframePeriodOrbits; // timeframe rate, in Hz
      theLog.log("Timeframe IDs generated by software, %.2lf Hz",
                 timeframeRate);
      timeframeClock.reset(1000000 / timeframeRate);
    } else {
      theLog.log("Timeframe IDs generated from RDH trigger counters");
    }

    // reset packetCounter monitor
    for (int i = 0; i <= RdhMaxLinkId; i++) {
      RdhLastPacketCounter[i] = 0;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << '\n'
              << boost::diagnostic_information(e) << "\n";
    return;
  }
  isInitialized = true;
}

ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {
  if (cfgRdhCheckEnabled) {
    theLog.log(
        "Equipment %s : %llu timeframes, %llu pages, RDH checks %llu ok, %llu "
        "errors, %llu stream inconsistencies %d packets dropped by CRU",
        name.c_str(), statsNumberOfTimeframes, statsNumberOfPages,
        statsRdhCheckOk, statsRdhCheckErr, statsRdhCheckStreamErr,
        lastPacketDropped);
  }
}

Thread::CallbackResult ReadoutEquipmentRORC::prepareBlocks() {
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
        theLog.log(InfoLogger::Severity::Warning,
                   "Equipment %s: CRU has dropped packets (new=%d total=%d)",
                   name.c_str(), newPacketDropped, currentPacketDropped);
        if (stopOnError) {
          theLog.log(InfoLogger::Severity::Error,
                     "Equipment %s: some data has been lost)", name.c_str());
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
  // this means we have not filled it fast enough (except in first loop, where
  // it's normal it is empty)
  if (isWaitingFirstLoop) {
    isWaitingFirstLoop = false;
  } else {
    int nFreeSlots = channel->getTransferQueueAvailable();
    if (nFreeSlots == RocFifoSize) {
      equipmentStats[EquipmentStatsIndexes::nFifoUpEmpty].increment();
    }
    equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].set(
        nFreeSlots);
  }

  // give free pages to the driver
  int nPushed = 0; // number of free pages pushed this iteration
  while (channel->getTransferQueueAvailable() != 0) {
    void *newPage = mp->getPage();
    if (newPage != nullptr) {
      // todo: check page is aligned as expected
      // optionnaly, cleanup page before use
      if (cfgCleanPageBeforeUse) {
        std::memset(newPage, 0, mp->getPageSize());
      }
      AliceO2::roc::Superpage superpage;
      superpage.setOffset((char *)newPage - (char *)mp->getBaseBlockAddress() +
                          pageSpaceReserved);
      superpage.setSize(superPageSize);
      // printf("pushed page %d\n",(int)superPageSize);
      superpage.setUserData(newPage);
      channel->pushSuperpage(superpage);
      // todo: break if push fails ( & release page to mp)
      isActive = 1;
      nPushed++;
    } else {
      equipmentStats[EquipmentStatsIndexes::nMemoryLow].increment();
      isActive = 0;
      break;
    }
  }
  equipmentStats[EquipmentStatsIndexes::nPushedUp].increment(nPushed);

  // check fifo occupancy ready queue size for stats
  equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].set(
      channel->getReadyQueueSize());
  if (channel->getReadyQueueSize() == RocFifoSize) {
    equipmentStats[EquipmentStatsIndexes::nFifoReadyFull].increment();
  }

  // if we have not put many pages (<25%) in ROC fifo, we can wait a bit
  if (nPushed < RocFifoSize / 4) {
    isActive = 0;
  }

  // This global mutex was also used as a fix to allow reading out 2 CRORC at
  // same time otherwise machine reboots when ACPI is not OFF
  // readoutEquipmentRORCLock.lock();

  // this is to be called periodically for driver internal business
  channel->fillSuperpages();

  // readoutEquipmentRORCLock.unlock();

  // from time to time, we may monitor temperature
  //      virtual boost::optional<float> getTemperature() = 0;

  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}

DataBlockContainerReference ReadoutEquipmentRORC::getNextBlock() {

  DataBlockContainerReference nextBlock = nullptr;

  // ensure the initialization was fine in the main thread
  if (!isInitialized) {
    return nullptr;
  }
  // channel->fillSuperpages();

  // check for completed page
  if ((channel->getReadyQueueSize() > 0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in
                                              // FIFO ... let's check its state
    void *mpPageAddress = (void *)(superpage.getUserData());
    if (superpage.isReady()) {
      std::shared_ptr<DataBlockContainer> d = nullptr;
      // printf ("received a page with %d bytes - isFilled=%d isREady=%d\n",
      //(int)superpage.getReceived(),(int)superpage.isFilled(),(int)superpage.isReady());
      try {
        if (pageSpaceReserved >= sizeof(DataBlock)) {
          d = mp->getNewDataBlockContainer(mpPageAddress);
        } else {
          // todo: allocate data block container elsewhere than beginning of
          // page
          // d=mp->getNewDataBlockContainer(nullptr);
          // d=mp->getNewDataBlockContainer((void *)(superpage.userData));
          // d=std::make_shared<DataBlockContainer>(nullptr);
        }
      } catch (...) {
        // todo: increment a stats counter?
        theLog.log("make_shared<DataBlock> failed");
      }
      if (d != nullptr) {
        channel->popSuperpage();
        statsNumberOfPages++;
        nextBlock = d;

        // printf("\nPage %llu\n",statsNumberOfPages);

        // in software clock mode, set timeframe id based on current timestamp
        if (usingSoftwareClock) {
          if (timeframeClock.isTimeout()) {
            currentTimeframe++;
            statsNumberOfTimeframes++;
            timeframeClock.increment();
          }
        }

        // default values for metadata
        int linkId = undefinedLinkId;
        int hbOrbit = -1;

        // retrieve metadata from RDH, if configured to do so
        if ((cfgRdhUseFirstInPageEnabled) || (cfgRdhCheckEnabled)) {
          RdhHandle h(d->getData()->data);

          // check that it is a correct RDH
          std::string errorDescription;
          if (h.validateRdh(errorDescription) != 0) {
            theLog.log(InfoLogger::Severity::Warning,
                       "First RDH in page is wrong: %s",
                       errorDescription.c_str());
          } else {

            // linkId
            linkId = h.getLinkId();

            // timeframe ID
            hbOrbit = h.getHbOrbit();
            // printf("HB orbit %u\n",hbOrbit);
            // printf("Page %llu, link %d, orbit
            // %u\n",statsNumberOfPages,linkId,hbOrbit);
            if ((statsNumberOfPages == 1) ||
                ((uint32_t)hbOrbit >=
                 currentTimeframeHbOrbitBegin + timeframePeriodOrbits)) {
              if (statsNumberOfPages == 1) {
                firstTimeframeHbOrbitBegin = hbOrbit;
              }
              statsNumberOfTimeframes++;
              currentTimeframeHbOrbitBegin =
                  hbOrbit - ((hbOrbit - firstTimeframeHbOrbitBegin) %
                             timeframePeriodOrbits); // keep it periodic and
                                                     // aligned to 1st timeframe
              uint64_t newTimeframe = 1 + (currentTimeframeHbOrbitBegin -
                                           firstTimeframeHbOrbitBegin) /
                                              timeframePeriodOrbits;
              if (newTimeframe != currentTimeframe + 1) {
                if (cfgRdhDumpErrorEnabled) {
                  theLog.log(InfoLogger::Severity::Warning,
                             "Non-contiguous timeframe IDs %llu ... %llu",
                             currentTimeframe, newTimeframe);
                }
              }
              currentTimeframe = newTimeframe;
              // printf("Starting timeframe %llu @ orbit %d (actual:
              // %d)\n",currentTimeframe,(int)currentTimeframeHbOrbitBegin,(int)hbOrbit);
            } else {
              // printf("HB orbit %d\n",hbOrbit);
            }
          }
        }

        // fill page metadata
        d->getData()->header.dataSize = superpage.getReceived();
        d->getData()->header.linkId = linkId;
        d->getData()->header.timeframeId = currentTimeframe;

        // Dump RDH if configured to do so
        if (cfgRdhDumpEnabled) {
          RdhBlockHandle b(d->getData()->data, d->getData()->header.dataSize);
          if (b.printSummary()) {
            printf("errors detected, suspending RDH dump\n");
            cfgRdhDumpEnabled = 0;
          } else {
            cfgRdhDumpEnabled++; // if value positive, it continues... but
                                 // negative, it stops on zero, to limit number
                                 // of dumps
          }
        }

        // validate RDH structure, if configured to do so
        if (cfgRdhCheckEnabled) {
          std::string errorDescription;
          size_t blockSize = d->getData()->header.dataSize;
          uint8_t *baseAddress = (uint8_t *)(d->getData()->data);
          int rdhIndexInPage = 0;

          for (size_t pageOffset = 0; pageOffset < blockSize;) {
            RdhHandle h(baseAddress + pageOffset);
            rdhIndexInPage++;

            // printf("RDH #%d @ 0x%X : next block @ +%d
            // bytes\n",rdhIndexInPage,(unsigned
            // int)pageOffset,h.getOffsetNextPacket());

            // data format:
            // RDH v3 =
            // https://docs.google.com/document/d/1otkSDYasqpVBDnxplBI7dWNxaZohctA-bvhyrzvtLoQ/edit?usp=sharing
            if (h.validateRdh(errorDescription)) {
              if ((cfgRdhDumpEnabled) || (cfgRdhDumpErrorEnabled)) {
                for (int i = 0; i < 16; i++) {
                  printf("%08X ", (int)(((uint32_t *)baseAddress)[i]));
                }
                printf("\n");
                printf("Page 0x%p + %ld\n%s", (void *)baseAddress, pageOffset,
                       errorDescription.c_str());
                h.dumpRdh();
                errorDescription.clear();
              }
              statsRdhCheckErr++;
              // stop on first RDH error (should distinguich valid/invalid block
              // length)
              break;
            } else {
              statsRdhCheckOk++;

              if (cfgRdhDumpEnabled) {
                h.dumpRdh();
                for (int i = 0; i < 16; i++) {
                  printf("%08X ",
                         (int)(((uint32_t *)baseAddress + pageOffset)[i]));
                }
                printf("\n");
              }
            }

            // linkId should be same everywhere in page
            if (linkId != h.getLinkId()) {
              if (cfgRdhDumpErrorEnabled) {
                theLog.log(InfoLogger::Severity::Warning,
                           "RDH #%d @ 0x%X : inconsistent link ids: %d != %d",
                           rdhIndexInPage, (unsigned int)pageOffset, linkId,
                           h.getLinkId());
              }
              statsRdhCheckStreamErr++;
              break; // stop checking this page
            }

            // check no timeframe overlap in page
            if ((uint32_t)hbOrbit >=
                currentTimeframeHbOrbitBegin + timeframePeriodOrbits) {
              if (cfgRdhDumpErrorEnabled) {
                theLog.log(InfoLogger::Severity::Warning,
                           "RDH #%d @ 0x%X : TimeFrame ID change in page not "
                           "allowed : hbOrbit %u > %u + %u",
                           rdhIndexInPage, (unsigned int)pageOffset,
                           (uint32_t)hbOrbit, currentTimeframeHbOrbitBegin,
                           timeframePeriodOrbits);
              }
              statsRdhCheckStreamErr++;
              break; // stop checking this page
            }

            // check packetCounter is contiguous
            if (cfgRdhCheckPacketCounterContiguous) {
              uint8_t newCount = h.getPacketCounter();
              // no boundary check necessary to verify linkId<=RdhMaxLinkId,
              // this was done in validateRDH()
              if (newCount != RdhLastPacketCounter[linkId]) {
                if (newCount !=
                    (uint8_t)(RdhLastPacketCounter[linkId] + (uint8_t)1)) {
                  theLog.log(InfoLogger::Severity::Warning,
                             "RDH #%d @ 0x%X : possible packets dropped for "
                             "link %d, packetCounter jump from %d to %d",
                             rdhIndexInPage, (unsigned int)pageOffset,
                             (int)linkId, (int)RdhLastPacketCounter[linkId],
                             (int)newCount);
                }
                RdhLastPacketCounter[linkId] = newCount;
              }
            }

            // TODO
            // check counter increasing
            // all have same TF id

            uint16_t offsetNextPacket = h.getOffsetNextPacket();
            if (offsetNextPacket == 0) {
              break;
            }
            pageOffset += offsetNextPacket;
          }
        }

      } else {
        // no data block container... what to do???
      }
    } else {
      // these are leftover pages not ready, discard them
      // todo: keep stats count
      // todo: check description of fifo workflow in rorc manual
      channel->popSuperpage();
      mp->releasePage(mpPageAddress);
      // printf("discard\n");
    }
  }
  return nextBlock;
}

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentRORC>(cfg, cfgEntryPoint);
}

void ReadoutEquipmentRORC::setDataOn() {
  if (isInitialized) {
    // start DMA
    theLog.log("Starting DMA for ROC %s", getName().c_str());
    channel->startDma();

    // get FIFO depth (it should be fully empty when starting)
    RocFifoSize = channel->getTransferQueueAvailable();
    theLog.log("ROC input queue size = %d pages", RocFifoSize);
    if (RocFifoSize == 0) {
      RocFifoSize = 1;
    }
  }
  ReadoutEquipment::setDataOn();
}

void ReadoutEquipmentRORC::setDataOff() {
  ReadoutEquipment::setDataOff(); // ensure we don't push pages any more
  sleep(1); // todo: driver should disable input fifo on stopdma()

  if (isInitialized) {
    theLog.log("Stopping DMA for ROC %s", getName().c_str());
    try {
      channel->stopDma();
    } catch (const std::exception &e) {
      theLog.log(InfoLogger::Severity::Error, "Exception : %s", e.what());
      theLog.log("%s", boost::diagnostic_information(e).c_str());
    }
  }
}
