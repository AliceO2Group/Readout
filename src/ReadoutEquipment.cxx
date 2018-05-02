#include "ReadoutEquipment.h"


#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;


ReadoutEquipment::ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint) {
  
  // example: browse config keys
  //for (auto cfgKey : ConfigFileBrowser (&cfg,"",cfgEntryPoint)) {
  //  std::string cfgValue=cfg.getValue<std::string>(cfgEntryPoint + "." + cfgKey);
  //  printf("%s.%s = %s\n",cfgEntryPoint.c_str(),cfgKey.c_str(),cfgValue.c_str());
  //}

  // by default, name the equipment as the config node entry point
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".name", name, cfgEntryPoint);

  // target readout rate in Hz, -1 for unlimited (default). Global parameter, same for all equipments.
  cfg.getOptionalValue<double>("readout.rate", readoutRate, -1.0);

  // idle sleep time, in microseconds.
  int cfgIdleSleepTime=200;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".idleSleepTime", cfgIdleSleepTime);

  // size of equipment output FIFO
  int cfgOutputFifoSize=1000;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".outputFifoSize", cfgOutputFifoSize);

  // get memory bank parameters  
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryBankName", memoryBankName);
  std::string cfgMemoryPoolPageSize="";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".memoryPoolPageSize",cfgMemoryPoolPageSize);
  memoryPoolPageSize=(int)ReadoutUtils::getNumberOfBytesFromString(cfgMemoryPoolPageSize.c_str());
  cfg.getOptionalValue<int>(cfgEntryPoint + ".memoryPoolNumberOfPages", memoryPoolNumberOfPages);

  // disable output?
  cfg.getOptionalValue<int>(cfgEntryPoint + ".disableOutput", disableOutput);

  // memory alignment
  std::string cfgStringFirstPageOffset="0";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".firstPageOffset", cfgStringFirstPageOffset);
  size_t cfgFirstPageOffset=(size_t)ReadoutUtils::getNumberOfBytesFromString(cfgStringFirstPageOffset.c_str());
  std::string cfgStringBlockAlign="2M";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".blockAlign", cfgStringBlockAlign);
  size_t cfgBlockAlign=(size_t)ReadoutUtils::getNumberOfBytesFromString(cfgStringBlockAlign.c_str());
    
  // log config summary
  theLog.log("Equipment %s: from config [%s], max rate=%lf Hz, idleSleepTime=%d us, outputFifoSize=%d", name.c_str(), cfgEntryPoint.c_str(), readoutRate, cfgIdleSleepTime, cfgOutputFifoSize);
  theLog.log("Equipment %s: requesting memory pool %d pages x %d bytes from bank %s, block aligned @ 0x%X, 1st page offset @ 0x%X", name.c_str(),(int) memoryPoolNumberOfPages, (int) memoryPoolPageSize, memoryBankName.c_str(),(int)cfgBlockAlign,(int)cfgFirstPageOffset);
  if (disableOutput) {
    theLog.log("Equipment %s: output DISABLED ! Data will be readout and dropped immediately",name.c_str());
  }
  
  // init stats
  equipmentStats.resize(EquipmentStatsIndexes::maxIndex);

  // create output fifo
  dataOut=std::make_shared<AliceO2::Common::Fifo<DataBlockContainerReference>>(cfgOutputFifoSize);
  if (dataOut==nullptr) {
    throw __LINE__;
  }
  
  // creation of memory pool for data pages
  // todo: also allocate pool of DataBlockContainers? at the same time? reserve space at start of pages?
  if ((memoryPoolPageSize<=0)||(memoryPoolNumberOfPages<=0)) {
     theLog.log("Equipment %s: wrong memory pool settings", name.c_str());
     throw __LINE__;
  } 
  pageSpaceReserved=sizeof(DataBlock); // reserve some data at beginning of each page for header, keep beginning of payload aligned as requested in config
  size_t firstPageOffset=0; // alignment of 1st page of memory pool
  if (cfgFirstPageOffset) {
    firstPageOffset=cfgFirstPageOffset-pageSpaceReserved;
  }
  theLog.log("pageSpaceReserved = %d, aligning 1st page @ 0x%X",(int)pageSpaceReserved,(int)firstPageOffset);
  mp=theMemoryBankManager.getPagedPool(memoryPoolPageSize, memoryPoolNumberOfPages, memoryBankName, firstPageOffset, cfgBlockAlign);
  if (mp==nullptr) {
    throw __LINE__;
  }

  // create thread
  readoutThread=std::make_unique<Thread>(ReadoutEquipment::threadCallback, this, name, cfgIdleSleepTime);
  if (readoutThread==nullptr) {
    throw __LINE__;
  }
}

const std::string & ReadoutEquipment::getName() {
  return name;
}

void ReadoutEquipment::start() {
  readoutThread->start();
  if (readoutRate>0) {
    clk.reset(1000000.0/readoutRate);
  }
  clk0.reset();
}

void ReadoutEquipment::stop() {
  
  double runningTime=clk0.getTime();
  readoutThread->stop();
  //printf("%llu blocks in %.3lf seconds => %.1lf block/s\n",nBlocksOut,clk0.getTimer(),nBlocksOut/clk0.getTime());
  readoutThread->join();
  
  for (int i=0; i<(int)EquipmentStatsIndexes::maxIndex; i++) {
    if (equipmentStats[i].getCount()) { 
      theLog.log("%s.%s = %lu  (avg=%.2lf  min=%lu  max=%lu  count=%lu)",name.c_str(),EquipmentStatsNames[i],equipmentStats[i].get(),equipmentStats[i].getAverage(),equipmentStats[i].getMinimum(),equipmentStats[i].getMaximum(),equipmentStats[i].getCount());
    } else {
      theLog.log("%s.%s = %lu",name.c_str(),EquipmentStatsNames[i],equipmentStats[i].get());    
    }
  }
  
  theLog.log("Average pages pushed per iteration: %.1f",equipmentStats[EquipmentStatsIndexes::nBlocksOut].get()*1.0/(equipmentStats[EquipmentStatsIndexes::nLoop].get()-equipmentStats[EquipmentStatsIndexes::nIdle].get()));
  theLog.log("Average fifoready occupancy: %.1f",equipmentStats[EquipmentStatsIndexes::fifoOccupancyFreeBlocks].get()*1.0/(equipmentStats[EquipmentStatsIndexes::nLoop].get()-equipmentStats[EquipmentStatsIndexes::nIdle].get()));
  theLog.log("Average data throughput: %s",ReadoutUtils::NumberOfBytesToString(equipmentStats[EquipmentStatsIndexes::nBytesOut].get()/runningTime,"Bytes/s").c_str());
}

ReadoutEquipment::~ReadoutEquipment() {
   // check if mempool still referenced
  if (!mp.unique()) {
    theLog.log("Equipment %s :  mempool still has %d references\n",name.c_str(),(int)mp.use_count());
  }
}




DataBlockContainerReference ReadoutEquipment::getBlock() {
  DataBlockContainerReference b=nullptr;
  dataOut->pop(b);
  return b;
}

Thread::CallbackResult  ReadoutEquipment::threadCallback(void *arg) {
  ReadoutEquipment *ptr=static_cast<ReadoutEquipment *>(arg);

  // flag to identify if something was done in this iteration
  bool isActive=false;
  
  for (;;) {
    ptr->equipmentStats[EquipmentStatsIndexes::nLoop].increment();

    // max number of blocks to read in this iteration.
    // this is a finite value to ensure all readout steps are done regularly.
    int maxBlocksToRead=1024;

    // check throughput
    if (ptr->readoutRate>0) {
      uint64_t nBlocksOut=ptr->equipmentStats[(int)EquipmentStatsIndexes::nBlocksOut].get(); // number of blocks we have already readout until now
      maxBlocksToRead=ptr->readoutRate*ptr->clk0.getTime()-nBlocksOut;
      if ((!ptr->clk.isTimeout()) && (nBlocksOut!=0) && (maxBlocksToRead<=0)) {
        // target block rate exceeded, wait a bit
        ptr->equipmentStats[EquipmentStatsIndexes::nThrottle].increment();
        break;
      }
    }

    // check status of output FIFO
    ptr->equipmentStats[EquipmentStatsIndexes::fifoOccupancyOutBlocks].set(ptr->dataOut->getNumberOfUsedSlots());

    // try to get new blocks
    int nPushedOut=0;
    for (int i=0;i<maxBlocksToRead;i++) {

      // check output FIFO status so that we are sure we can push next block, if any
      if (ptr->dataOut->isFull()) {
        ptr->equipmentStats[EquipmentStatsIndexes::nOutputFull].increment();
        break;
      }

      // get next block
      DataBlockContainerReference nextBlock=ptr->getNextBlock();
      if (nextBlock==nullptr) {
        break;
      }

      if (!ptr->disableOutput) {
        // push new page to output fifo
        ptr->dataOut->push(nextBlock);
      }
      
      // update rate-limit clock
      if (ptr->readoutRate>0) {
        ptr->clk.increment();
      }

      // update stats
      nPushedOut++;
      ptr->equipmentStats[EquipmentStatsIndexes::nBytesOut].increment(nextBlock->getData()->header.dataSize);   
      isActive=true;
    }
    ptr->equipmentStats[EquipmentStatsIndexes::nBlocksOut].increment(nPushedOut);
    

    // prepare next blocks
    Thread::CallbackResult statusPrepare=ptr->prepareBlocks();
    switch (statusPrepare) {
      case (Thread::CallbackResult::Ok):
        isActive=true;
        break;
      case (Thread::CallbackResult::Idle):
        break;      
      default:
        // this is an abnormal situation, return corresponding status
        return statusPrepare;
    }


    // consider inactive if we have not pushed much compared to free space in output fifo
    // todo: instead, have dynamic 'inactive sleep time' as function of actual outgoing page rate to optimize polling interval
    if (nPushedOut<ptr->dataOut->getNumberOfFreeSlots()/4) {
// disabled, should not depend on output fifo size
//      isActive=0;
    }
      
    // todo: add SLICER to aggregate together time-range data
    // todo: get other FIFO status

    break;
  }

  if (!isActive) {
    ptr->equipmentStats[EquipmentStatsIndexes::nIdle].increment();
    return Thread::CallbackResult::Idle;
  }
  return Thread::CallbackResult::Ok;
}
