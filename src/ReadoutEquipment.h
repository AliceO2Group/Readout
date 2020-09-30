// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <Common/Configuration.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>
#include <Common/Timer.h>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"

#include <memory>

#include "CounterStats.h"
#include "MemoryHandler.h"

#include "MemoryBankManager.h"
#include "RdhUtils.h"

using namespace AliceO2::Common;

class ReadoutEquipment {
public:
  ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint);
  virtual ~ReadoutEquipment();

  DataBlockContainerReference getBlock();

  void start();
  void stop();
  const std::string &getName();

  // enable / disable data production by the equipment
  virtual void setDataOn();
  virtual void setDataOff();

  // initialize / finalize counters (called before 1st loop and after last loop)
  virtual void initCounters();
  virtual void finalCounters();

  bool stopOnError = false; // if set, readout will stop when this equipment
                            // reports an error (isError flag)
  int isError = 0; // flag which might be used to count number of errors
                   // occuring in the equipment

  // protected:
  // todo: give direct access to output FIFO?
  std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> dataOut;

  // get current memory pool usage (available and total)
  int getMemoryUsage(size_t &numberOfPagesAvailable,
                     size_t &numberOfPagesInPool);

private:
  std::unique_ptr<Thread> readoutThread;
  static Thread::CallbackResult threadCallback(void *arg);

  // Function called iteratively in dedicated thread to populate FIFO.
  // The equipmentStats member variable should be updated.
  // calling sequence: prepareBlocks() + iterate getNextBlock()
  // The return value gives a hint about if it should be called soon again or
  // not. If idle, can wait a bit
  virtual Thread::CallbackResult prepareBlocks() {
    return Thread::CallbackResult::Idle;
  };
  virtual DataBlockContainerReference getNextBlock() { return nullptr; };

  DataBlockId currentBlockId; // current block id

protected:
  // data enabled ? controlled by setDataOn/setDataOff
  bool isDataOn = false;

  // Definition of performance counters for readout statistics.
  // Each counter is assigned a unique integer index (incremental, starting 0).
  // The last element can be used to get the number of counters defined.
  // The (int) index value can be used to access the corresponding counter in
  // equipmentStats array.
  enum EquipmentStatsIndexes {
    nBlocksOut = 0,
    nBytesOut = 1,
    nMemoryLow = 2,
    nOutputFull = 3,
    nIdle = 4,
    nLoop = 5,
    nThrottle = 6, // when rate throtthling was done
    nFifoUpEmpty =
        7, // we call fifoUP the one where we push upstream pages to be filled
    nFifoReadyFull =
        8,         // we call fifoReady the one where ROC pushes ready pages
    nPushedUp = 9, // free pages pushed upstream
    fifoOccupancyFreeBlocks = 10,
    fifoOccupancyReadyBlocks = 11,
    fifoOccupancyOutBlocks = 12,
    nPagesUsed = 13, // number of used pages in memory pool
    nPagesFree = 14, // number of free pages in memory pool
    maxIndex = 15    // not a counter, used to know number of elements in enum
  };

  // Display names of the performance counters.
  // Should be in same order as in enum.
  const char *EquipmentStatsNames[EquipmentStatsIndexes::maxIndex] = {
      "nBlocksOut",
      "nBytesOut",
      "nMemoryLow",
      "nOutputFull",
      "nIdle",
      "nLoop",
      "nThrottle",
      "nFifoUpEmpty",
      "nFifoReadyFull",
      "nPushedUp",
      "fifoOccupancyFreeBlocks",
      "fifoOccupancyReadyBlocks",
      "fifoOccupancyOutBlocks",
      "nPagesUsed",
      "nPagesFree"};

  // check consistency (size) of EquipmentStatsNames with EquipmentStatsIndexes
  static_assert((sizeof(EquipmentStatsNames) /
                 sizeof(EquipmentStatsNames[0])) ==
                    EquipmentStatsIndexes::maxIndex,
                "EquipmentStatsNames size mismatch EquipmentStatsIndexes::");

  // Counter values, updated at runtime
  std::vector<CounterStats> equipmentStats;
  std::vector<CounterValue> equipmentStatsLast;

  double cfgConsoleStatsUpdateTime =
      0; // number of seconds between regular printing of statistics on console
         // (if zero, only on stop)
  AliceO2::Common::Timer
      consoleStatsTimer; // timer to keep track of elapsed time between console
                         // statistics updates

  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;

  double readoutRate;
  std::string name; // name of the equipment

  uint16_t id = undefinedEquipmentId; // id of equipment (optional, used to tag
                                      // data blocks)

  std::shared_ptr<MemoryPagesPool>
      mp;                     // a memory pool from which to allocate data pages
  int memoryPoolPageSize = 0; // size if each page in pool
  int memoryPoolNumberOfPages = 0; // number of pages in pool
  std::string memoryBankName = ""; // memory bank to be used. by default, this
                                   // uses the first memory bank available

  int disableOutput =
      0; // when set true, data are dropped before pushing to output queue

  size_t pageSpaceReserved =
      0; // amount of space reserved (in bytes) at beginning of each data page,
         // possibly to store header

  int debugFirstPages = 0; // print debug info on first number of pages read

private:
  
  int tagDatablockFromRdh(RdhHandle &RDH, DataBlockHeaderBase &h);
  unsigned long long statsNumberOfTimeframes =
      0; // number of timeframes read out
  uint32_t currentTimeframeHbOrbitBegin =
      0; // HbOrbit of beginning of timeframe
  uint32_t firstTimeframeHbOrbitBegin =
      0; // HbOrbit of beginning of first timeframe
  bool isDefinedFirstTimeframeHbOrbitBegin = 0;

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
  double timeframeRate = 0; // timeframe rate, when generated internally

  // RDH-related configuration parameters
  
  int cfgRdhCheckEnabled = 0;     // flag to enable RDH check at runtime
  int cfgRdhDumpEnabled = 0;      // flag to enable RDH dump at runtime
  int cfgRdhDumpErrorEnabled = 1; // flag to enable RDH error log at runtime
  int cfgRdhDumpWarningEnabled = 0; // flag to enable RDH warning log at runtime
  int cfgRdhUseFirstInPageEnabled = 0; // flag to enable reading of first RDH in
                                       // page to populate readout headers
  int cfgRdhCheckPacketCounterContiguous =
      1; // flag to enable checking if RDH packetCounter value contiguous (done
         // link-by-link)

  bool isRdhEquipment = false; // to be set true for RDH equipments
  
  int processRdh(DataBlockContainerReference &nextBlock);
  
protected:
  // get timeframe from orbit
  // orbit of TF 1 is set on first call
  uint64_t getTimeframeFromOrbit(uint32_t orbit);
  uint64_t getCurrentTimeframe();
  
  uint32_t getTimeframePeriodOrbits() {
    return timeframePeriodOrbits;
  }

  // compute range of orbits for given timeframe
  void getTimeframeOrbitRange(uint64_t tfId, uint32_t hbOrbitMin, uint32_t hbOrbitMax);

  void initRdhEquipment(); // to be called by equipments producing RDH-formatted data
  
  unsigned long long statsRdhCheckOk =
      0; // number of RDH structs which have passed check ok
  unsigned long long statsRdhCheckErr =
      0; // number of RDH structs which have not passed check
  unsigned long long statsRdhCheckStreamErr =
      0; // number of inconsistencies in RDH stream (e.g. ids/timing compared to
         // previous RDH)


};

std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentCruEmulator(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentPlayer(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment>
getReadoutEquipmentZmq(ConfigFile &cfg, std::string cfgEntryPoint);
