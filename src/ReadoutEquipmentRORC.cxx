#include "ReadoutEquipment.h"

#include <ReadoutCard/Parameters.h>
#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>


#include <string>
#include <mutex>

#include <Common/Timer.h>

#include "ReadoutUtils.h"
#include "RdhUtils.h"

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;



class ReadoutEquipmentRORC : public ReadoutEquipment {

  public:
    ReadoutEquipmentRORC(ConfigFile &cfg, std::string name="rorcReadout");
    ~ReadoutEquipmentRORC();

  private:
    Thread::CallbackResult prepareBlocks();
    DataBlockContainerReference getNextBlock(); 
    void setDataOn();
    void setDataOff();
 
    Thread::CallbackResult  populateFifoOut(); // the data readout loop function
    
    AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel;    // channel to ROC device

    DataBlockId currentId=0;    // current data id, kept for auto-increment
       
    bool isInitialized=false;     // flag set to 1 when class has been successfully initialized
    bool isWaitingFirstLoop=true;  // flag set until first readout loop called

    int RocFifoSize=0;  // detected size of ROC fifo (when filling it for the first time)

    int cfgRdhCheckEnabled=0; // flag to enable RDH check at runtime
    int cfgRdhDumpEnabled=0;  // flag to enable RDH dump at runtime

    unsigned long long statsRdhCheckOk=0;   // number of RDH structs which have passed check ok
    unsigned long long statsRdhCheckErr=0;  // number of RDH structs which have not passed check    
    unsigned long long statsNumberOfPages=0; // number of pages read out
    unsigned long long statsNumberOfTimeframes=0; // number of timeframes read out
    
    
    AliceO2::Common::Timer timeframeClock;	// timeframe id should be increased at each clock cycle
    int currentTimeframe=0;	                // id of current timeframe
    bool usingSoftwareClock=false;              // if set, using internal software clock to generate timeframe id

    const unsigned int LHCBunches=3564;    // number of bunches in LHC
    const unsigned int LHCOrbitRate=11246; // LHC orbit rate, in Hz. 299792458 / 26659
    const uint32_t timeframePeriodOrbits=256;   // timeframe interval duration in number of LHC orbits
    
    uint32_t currentTimeframeHbOrbitBegin=0; // HbOrbit of beginning of timeframe 
    uint32_t firstTimeframeHbOrbitBegin=0; // HbOrbit of beginning of first timeframe
        
    size_t superPageSize=0; // usable size of a superpage
};


//std::mutex readoutEquipmentRORCLock;



struct ReadoutEquipmentRORCException : virtual Exception {};

ReadoutEquipmentRORC::ReadoutEquipmentRORC(ConfigFile &cfg, std::string name) : ReadoutEquipment(cfg, name) {
   
  try {

    // get parameters from configuration
    // config keys are the same as the corresponding set functions in AliceO2::roc::Parameters
    
    // configuration parameter: | equipment-rorc-* | cardId | string | | ID of the board to be used. Typically, a PCI bus device id. c.f. AliceO2::roc::Parameters. |
    std::string cardId=cfg.getValue<std::string>(name + ".cardId");
    
    // configuration parameter: | equipment-rorc-* | channelNumber | int | 0 | Channel number of the board to be used. Typically 0 for CRU, or 1-6 for CRORC. c.f. AliceO2::roc::Parameters. |
    int cfgChannelNumber=0;
    cfg.getOptionalValue<int>(name + ".channelNumber", cfgChannelNumber);

    // configuration parameter: | equipment-rorc-* | generatorEnabled | int | 0 | If non-zero, enable card internal generator. c.f. AliceO2::roc::Parameters. |
    int cfgGeneratorEnabled=0;
    cfg.getOptionalValue<int>(name + ".generatorEnabled", cfgGeneratorEnabled);
    
    // configuration parameter: | equipment-rorc-* | generatorDataSize | int | 8192 | If generatorEnabled, defines size of data generated. c.f. AliceO2::roc::Parameters. |
    int cfgGeneratorDataSize=8192;
    cfg.getOptionalValue<int>(name + ".generatorDataSize", cfgGeneratorDataSize);
    
    // configuration parameter: | equipment-rorc-* | generatorLoopback | string | INTERNAL | If generatorEnabled, defines loopback mode. Otherwise, parameter automatically set to NONE. Possible values: NONE, DIU, SIU, INTERNAL. c.f. AliceO2::roc::Parameters. |
    std::string cfgGeneratorLoopback="INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".generatorLoopback", cfgGeneratorLoopback);

    // configuration parameter: | equipment-rorc-* | generatorPattern | string | INCREMENTAL | If generatorEnabled, defines pattern of data generated. Possible values: ALTERNATING,CONSTANT,DECREMENTAL, FLYING_0, FLYING_1, INCREMENTAL, RANDOM, UNKNOWN. c.f. AliceO2::roc::Parameters. |
    std::string cfgGeneratorPattern="INCREMENTAL";
    cfg.getOptionalValue<std::string>(name + ".generatorPattern", cfgGeneratorPattern);
 
     // configuration parameter: | equipment-rorc-* | generatorRandomSizeEnabled | int | 0 | Enable (value=1) or disable (value=0) random size when using internal data generator. c.f. AliceO2::roc::Parameters. |  
    int cfgGeneratorRandomSizeEnabled=0;
    cfg.getOptionalValue<int>(name + ".generatorRandomSizeEnabled", cfgGeneratorRandomSizeEnabled);
    
    // configuration parameter: | equipment-rorc-* | linkMask | string | 0-31 | List of links to be enabled. For CRU, in the 0-31 range. Can be a single value, a comma-separated list, a range or comma-separated list of ranges. c.f. AliceO2::roc::Parameters. |
    std::string cfgLinkMask="0-31";
    cfg.getOptionalValue<std::string>(name + ".linkMask", cfgLinkMask);
    
    //std::string cfgReadoutMode="CONTINUOUS";
    //cfg.getOptionalValue<std::string>(name + ".readoutMode", cfgReadoutMode);
    
    // configuration parameter: | equipment-rorc-* | resetLevel | string | INTERNAL | Reset level of the device. Can be one of NOTHING, INTERNAL, INTERNAL_DIU, INTERNAL_DIU_SIU. c.f. AliceO2::roc::Parameters. |
    std::string cfgResetLevel="INTERNAL";
    cfg.getOptionalValue<std::string>(name + ".resetLevel", cfgResetLevel);

    // extra configuration parameters 
    // configuration parameter: | equipment-rorc-* | rdhCheckEnabled | int | 0 | If set, data pages are parsed and RDH headers checked. Errors are reported in logs. |
    cfg.getOptionalValue<int>(name + ".rdhCheckEnabled", cfgRdhCheckEnabled);
    // configuration parameter: | equipment-rorc-* | rdhDumpEnabled | int | 0 | If set, data pages are parsed and RDH headers summary printed. Setting a negative number will print only the first N RDH.|
    cfg.getOptionalValue<int>(name + ".rdhDumpEnabled", cfgRdhDumpEnabled);
        
/*    // get readout memory buffer parameters
    std::string sMemorySize=cfg.getValue<std::string>(name + ".memoryBufferSize");
    std::string sPageSize=cfg.getValue<std::string>(name + ".memoryPageSize");
    long long mMemorySize=ReadoutUtils::getNumberOfBytesFromString(sMemorySize.c_str());
    long long mPageSize=ReadoutUtils::getNumberOfBytesFromString(sPageSize.c_str());

    std::string cfgHugePageSize="1GB";
    cfg.getOptionalValue<std::string>(name + ".memoryHugePageSize",cfgHugePageSize);
*/
    // unique identifier based on card ID
    std::string uid="readout." + cardId + "." + std::to_string(cfgChannelNumber);
    //sleep((cfgChannelNumber+1)*2);  // trick to avoid all channels open at once - fail to acquire lock
    
    // define usable superpagesize
    superPageSize=mp->getPageSize()-pageSpaceReserved; // Keep space at beginning for DataBlock object
    superPageSize-=superPageSize % (32*1024); // Must be a multiple of 32Kb for ROC
    theLog.log("Using superpage size %ld",superPageSize);
    if (superPageSize==0) {
      BOOST_THROW_EXCEPTION(ReadoutEquipmentRORCException() << ErrorInfo::Message("Superpage must be at least 32kB"));
    }
  
    // open and configure ROC
    theLog.log("Opening ROC %s:%d",cardId.c_str(),cfgChannelNumber);
    AliceO2::roc::Parameters params;
    params.setCardId(AliceO2::roc::Parameters::cardIdFromString(cardId));   
    params.setChannelNumber(cfgChannelNumber);

    // setDmaPageSize() : seems deprecated, let's not configure it

    // generator related parameters
    params.setGeneratorEnabled(cfgGeneratorEnabled);
    if (cfgGeneratorEnabled) {
      params.setGeneratorDataSize(cfgGeneratorDataSize);
      params.setGeneratorLoopback(AliceO2::roc::LoopbackMode::fromString(cfgGeneratorLoopback));
      params.setGeneratorPattern(AliceO2::roc::GeneratorPattern::fromString(cfgGeneratorPattern));
      params.setGeneratorRandomSizeEnabled(cfgGeneratorRandomSizeEnabled);
    } else {
      // for some unknown reason, one has to explicitely disable the loopback when not using the generator
      params.setGeneratorLoopback(AliceO2::roc::LoopbackMode::None);
    }   

    // card readout mode : experimental, not needed
    // params.setReadoutMode(AliceO2::roc::ReadoutMode::fromString(cfgReadoutMode));    
  
    /*
    theLog.log("Loop DMA block %p:%lu", mp->getBaseBlockAddress(), mp->getBaseBlockSize());
    char *ptr=(char *)mp->getBaseBlockAddress();
    for (size_t i=0;i<mp->getBaseBlockSize();i++) {
      ptr[i]=0;
    }
    */

    // register the memory block for DMA
    void *baseAddress=(void *)mp->getBaseBlockAddress();
    size_t blockSize=mp->getBaseBlockSize();
    theLog.log("Register DMA block %p:%lu",baseAddress,blockSize);
    params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory {
       baseAddress, blockSize
    });
       
    // define link mask
    // this is harmless for C-RORC
    params.setLinkMask(AliceO2::roc::Parameters::linkMaskFromString(cfgLinkMask));

    // open channel with above parameters
    channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);  
    channel->resetChannel(AliceO2::roc::ResetLevel::fromString(cfgResetLevel));

    // retrieve card information
    std::string infoPciAddress=channel->getPciAddress().toString();
    int infoNumaNode=channel->getNumaNode();
    std::string infoSerialNumber="unknown";
    auto v_infoSerialNumber=channel->getSerial();
    if (v_infoSerialNumber) {
      infoSerialNumber=std::to_string(v_infoSerialNumber.get());
    }
    std::string infoFirmwareVersion=channel->getFirmwareInfo().value_or("unknown");
    std::string infoCardId=channel->getCardId().value_or("unknown");
    theLog.log("Equipment %s : PCI %s @ NUMA node %d, serial number %s, firmware version %s, card id %s", name.c_str(), infoPciAddress.c_str(), infoNumaNode, infoSerialNumber.c_str(),
    infoFirmwareVersion.c_str(), infoCardId.c_str());
    

    // todo: log parameters ?
    usingSoftwareClock=true;

    // reset timeframe id
    currentTimeframe=0;
    if (!cfgRdhCheckEnabled) {
      usingSoftwareClock=true; // if RDH disabled, use internal clock for TF id
    }
    if (usingSoftwareClock) {
      // reset timeframe clock
      double timeframeRate=LHCOrbitRate*1.0/timeframePeriodOrbits; // timeframe rate, in Hz
      theLog.log("Timeframe IDs generated by software, %.2lf Hz",timeframeRate);
      timeframeClock.reset(1000000/timeframeRate);
    } else {
      theLog.log("Timeframe IDs generated from RDH trigger counters");
    }

  }
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << '\n' << boost::diagnostic_information(e) << "\n";
    return;
  }
  isInitialized=true;
}



ReadoutEquipmentRORC::~ReadoutEquipmentRORC() {
  if (cfgRdhCheckEnabled) {
    theLog.log("Equipment %s : %llu timeframes, %llu pages, RDH checks %llu ok, %llu errors",name.c_str(),statsNumberOfTimeframes,statsNumberOfPages,statsRdhCheckOk,statsRdhCheckErr);  
  }
}


Thread::CallbackResult ReadoutEquipmentRORC::prepareBlocks(){
  if (!isInitialized) return  Thread::CallbackResult::Error;
  if (!isDataOn) return  Thread::CallbackResult::Idle;
  
  int isActive=0;
  
  // keep track of situations where the queue is completely empty
  // this means we have not filled it fast enough (except in first loop, where it's normal it is empty)
  if (isWaitingFirstLoop) {
    isWaitingFirstLoop=false;
  } else {
    int nFreeSlots=channel->getTransferQueueAvailable();
    if (nFreeSlots == RocFifoSize) {  
      equipmentStats[EquipmentStatsIndexes::nFifoUpEmpty].increment();
    }
    equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].set(nFreeSlots);
  }
  
  // give free pages to the driver
  int nPushed=0;  // number of free pages pushed this iteration
  while (channel->getTransferQueueAvailable() != 0) {
    void *newPage=mp->getPage();
    if (newPage!=nullptr) {   
      // todo: check page is aligned as expected      
      AliceO2::roc::Superpage superpage;
      superpage.setOffset((char *)newPage-(char *)mp->getBaseBlockAddress()+pageSpaceReserved);
      superpage.setSize(superPageSize);
      superpage.setUserData(newPage);
      channel->pushSuperpage(superpage);      
      isActive=1;
      nPushed++;
    } else {
      equipmentStats[EquipmentStatsIndexes::nMemoryLow].increment();
      isActive=0;
      break;
    }
  }
  equipmentStats[EquipmentStatsIndexes::nPushedUp].increment(nPushed);

  // check fifo occupancy ready queue size for stats
  equipmentStats[EquipmentStatsIndexes::fifoOccupancyReadyBlocks].set(channel->getReadyQueueSize());
  if (channel->getReadyQueueSize()==RocFifoSize) {
    equipmentStats[EquipmentStatsIndexes::nFifoReadyFull].increment();  
  }

  // if we have not put many pages (<25%) in ROC fifo, we can wait a bit
  if (nPushed<RocFifoSize/4) { 
    isActive=0;
  }


  // This global mutex was also used as a fix to allow reading out 2 CRORC at same time
  // otherwise machine reboots when ACPI is not OFF
  //readoutEquipmentRORCLock.lock();
  
  // this is to be called periodically for driver internal business
  channel->fillSuperpages();
  
  //readoutEquipmentRORCLock.unlock();


  // from time to time, we may monitor temperature
//      virtual boost::optional<float> getTemperature() = 0;


  if (!isActive) {
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}


DataBlockContainerReference ReadoutEquipmentRORC::getNextBlock() {

  DataBlockContainerReference nextBlock=nullptr;
  
  // ensure the initialization was fine in the main thread
  if (!isInitialized) {
  	return nullptr;
  }  
  //channel->fillSuperpages();
     
  // check for completed page
  if ((channel->getReadyQueueSize()>0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in FIFO ... let's check its state
    if (superpage.isFilled()) {
      std::shared_ptr<DataBlockContainer>d=nullptr;
//      printf ("received a page with %d bytes - isFilled=%d isREady=%d\n",(int)superpage.getReceived(),(int)superpage.isFilled(),(int)superpage.isReady());
      try {
        if (pageSpaceReserved>=sizeof(DataBlock)) {
          d=mp->getNewDataBlockContainer((void *)(superpage.getUserData()));
        } else {
          // todo: allocate data block container elsewhere than beginning of page
          //d=mp->getNewDataBlockContainer(nullptr);        
          //d=mp->getNewDataBlockContainer((void *)(superpage.userData));
          //d=std::make_shared<DataBlockContainer>(nullptr);
        }
      }
      catch (...) {
        // todo: increment a stats counter?
        theLog.log("make_shared<DataBlock> failed");
      }
      if (d!=nullptr) {
        statsNumberOfPages++;
        
        d->getData()->header.dataSize=superpage.getReceived();
        d->getData()->header.linkId=0; // TODO

        channel->popSuperpage();
        nextBlock=d;
        
	//printf("\nPage %llu\n",statsNumberOfPages);	
	
        // validate RDH structure, if configured to do so
        int linkId=-1;
        int hbOrbit=-1;
                
        // checks to do:
        // - HB clock consistent in all RDHs
        // - increasing counters
	if (cfgRdhDumpEnabled) {
	  RdhBlockHandle b(d->getData()->data,d->getData()->header.dataSize);
	  if (b.printSummary()) {
	    printf("errors detected, suspending RDH dump\n");
	    cfgRdhDumpEnabled=0;
	  } else {
  	    cfgRdhDumpEnabled++; //if value positive, it continues... but negative, it stops on zero, to limit number of dumps
	  }
	}

        if (cfgRdhCheckEnabled) {
          std::string errorDescription;
          size_t blockSize=d->getData()->header.dataSize;
          uint8_t *baseAddress=(uint8_t *)(d->getData()->data);
	  int rdhIndexInPage=0;
	  
          for (size_t pageOffset=0;pageOffset<blockSize;) {
            RdhHandle h(baseAddress+pageOffset);
            rdhIndexInPage++;
	    
            //printf("RDH #%d @ 0x%X : next block @ +%d bytes\n",rdhIndexInPage,(unsigned int)pageOffset,h.getOffsetNextPacket());
	    
            if (linkId==-1) {
              linkId=h.getLinkId();
	      //printf("Page %llu, link %d\n",statsNumberOfPages,linkId);	
	      //printf("Page for link %d\n",linkId);	
            } else {
              if (linkId!=h.getLinkId()) {
                printf("RDH #%d @ 0x%X : inconsistent link ids: %d != %d\n",rdhIndexInPage,(unsigned int)pageOffset,linkId,h.getLinkId());
		break; // stop checking this page
              }
            }
            
            if (hbOrbit==-1) {
              hbOrbit=h.getHbOrbit();
	      //printf("HB orbit %u\n",hbOrbit);
	      //printf("Page %llu, link %d, orbit %u\n",statsNumberOfPages,linkId,hbOrbit);
              if ((statsNumberOfPages==1) || ((uint32_t)hbOrbit>=currentTimeframeHbOrbitBegin+timeframePeriodOrbits)) {
                if (statsNumberOfPages==1) {
                  firstTimeframeHbOrbitBegin=hbOrbit;
                }
                statsNumberOfTimeframes++;
                currentTimeframeHbOrbitBegin=hbOrbit-((hbOrbit-firstTimeframeHbOrbitBegin)%timeframePeriodOrbits); // keep it periodic and aligned to 1st timeframe
                int newTimeframe=1+(currentTimeframeHbOrbitBegin-firstTimeframeHbOrbitBegin)/timeframePeriodOrbits;
                if (newTimeframe!=currentTimeframe+1) {
                  printf("Non-contiguous timeframe IDs %d ... %d\n",currentTimeframe,newTimeframe);
                }
                currentTimeframe=newTimeframe;
                 //printf("Starting timeframe %d @ orbit %d (actual: %d)\n",currentTimeframe,(int)currentTimeframeHbOrbitBegin,(int)hbOrbit);
              } else {
                 //printf("HB orbit %d\n",hbOrbit);
              }
              
            }           
            
            //data format:
            // RDH v3 = https://docs.google.com/document/d/1otkSDYasqpVBDnxplBI7dWNxaZohctA-bvhyrzvtLoQ/edit?usp=sharing
            if (h.validateRdh(errorDescription)) {
	      bool cfgRdhDumpErrorEnabled=1;
              if ((cfgRdhDumpEnabled)||(cfgRdhDumpErrorEnabled)) {
                for (int i=0;i<16;i++) {
                  printf("%08X ",(int)(((uint32_t*)baseAddress)[i]));
                }
                printf("\n");
                printf("Page 0x%p + %ld\n%s",(void *)baseAddress,pageOffset,errorDescription.c_str());
                h.dumpRdh();
                errorDescription.clear();
              }
              statsRdhCheckErr++;
	      // stop on first RDH error (should distinguich valid/invalid block length)
	      break;
            } else {
              statsRdhCheckOk++;

              if (cfgRdhDumpEnabled) {
                h.dumpRdh();
                for (int i=0;i<16;i++) {
                  printf("%08X ",(int)(((uint32_t*)baseAddress+pageOffset)[i]));
                }
                printf("\n");

              }
            }
	    uint16_t offsetNextPacket=h.getOffsetNextPacket();
	    if (offsetNextPacket==0) {
	      break;
	    }
	    pageOffset+=offsetNextPacket;            
          }
        }
        if (linkId>=0) {
          d->getData()->header.linkId=linkId;
        }

        if (usingSoftwareClock) {
	  if (timeframeClock.isTimeout()) {
	    currentTimeframe++;
            statsNumberOfTimeframes++;
	    timeframeClock.increment();
	  }
        }

        // set timeframe id
        d->getData()->header.id=currentTimeframe;        
      }
      else {
        // no data block container... what to do???
      }
    }
  }
  return nextBlock;
}


std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ReadoutEquipmentRORC>(cfg,cfgEntryPoint);
}


void ReadoutEquipmentRORC::setDataOn() {
  if (isInitialized) {
    // start DMA    
    theLog.log("Starting DMA for ROC %s",getName().c_str());
    channel->startDma();

    // get FIFO depth (it should be fully empty when starting)
    RocFifoSize=channel->getTransferQueueAvailable();
    theLog.log("ROC input queue size = %d pages",RocFifoSize);
    if (RocFifoSize==0) {RocFifoSize=1;}
  }
  ReadoutEquipment::setDataOn();
}

void ReadoutEquipmentRORC::setDataOff() {
  if (isInitialized) {
    theLog.log("Stopping DMA for ROC %s",getName().c_str());
    channel->stopDma();
  }
  ReadoutEquipment::setDataOff();
}
