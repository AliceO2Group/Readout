#include <Common/Configuration.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>
#include <Common/Timer.h>

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>

#include "CounterStats.h"
#include "MemoryHandler.h"

#include "MemoryBankManager.h"


using namespace AliceO2::Common;


class ReadoutEquipment {
  public:
  ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint);
  virtual ~ReadoutEquipment();

  DataBlockContainerReference getBlock();

  void start();
  void stop();
  const std::string & getName();

// protected:
// todo: give direct access to output FIFO?
  std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> dataOut;

  private:
  std::unique_ptr<Thread> readoutThread;
  static Thread::CallbackResult threadCallback(void *arg);

  // Function called iteratively in dedicated thread to populate FIFO.
  // The equipmentStats member variable should be updated.
  // calling sequence: prepareBlocks() + iterate getNextBlock()
  // The return value gives a hint about if it should be called soon again or not. If idle, can wait a bit
  virtual Thread::CallbackResult prepareBlocks() {return Thread::CallbackResult::Idle;};
  virtual DataBlockContainerReference getNextBlock() {return nullptr;};


  protected:
    
  // Definition of performance counters for readout statistics.
  // Each counter is assigned a unique integer index (incremental, starting 0).
  // The last element can be used to get the number of counters defined.
  // The (int) index value can be used to access the corresponding counter in equipmentStats array.
  enum EquipmentStatsIndexes {
    nBlocksOut = 0,
    nBytesOut = 1,
    nMemoryLow = 2,
    nOutputFull = 3,
    nIdle=4,
    nLoop=5,
    nThrottle = 6 , // when rate throtthling was done
    nFifoUpEmpty = 7, // we call fifoUP the one where we push upstream pages to be filled
    nFifoReadyFull = 8, // we call fifoReady the one where ROC pushes ready pages
    nPushedUp = 9, // free pages pushed upstream
    fifoOccupancyFreeBlocks = 10,
    fifoOccupancyReadyBlocks = 11,
    fifoOccupancyOutBlocks = 12,
    maxIndex=13 // not a counter, used to know number of elements in enum
  };
  
  // Display names of the performance counters.
  // Should be in same order as in enum.
  const char *EquipmentStatsNames[EquipmentStatsIndexes::maxIndex] = {
   "nBlocksOut", "nBytesOut", "nMemoryLow", "nOutputFull", "nIdle", "nLoop", "nThrottle", "nFifoUpEmpty", "nFifoReadyFull", "nPushedUp", "fifoOccupancyFreeBlocks", "fifoOccupancyReadyBlocks", "fifoOccupancyOutBlocks"
  };

  // check consistency (size) of EquipmentStatsNames with EquipmentStatsIndexes
  static_assert((sizeof(EquipmentStatsNames)/sizeof(EquipmentStatsNames[0]))==EquipmentStatsIndexes::maxIndex,"EquipmentStatsNames size mismatch EquipmentStatsIndexes::");

  // Counter values, updated at runtime
  std::vector<CounterStats> equipmentStats;
   
  
  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;

  // unsigned long long nBlocksOut;
  double readoutRate;
  std::string name;  // name of the equipment
  
  std::shared_ptr<MemoryPagesPool> mp; // a memory pool from which to allocate data pages
  int memoryPoolPageSize=0; // size if each page in pool
  int memoryPoolNumberOfPages=0; // number of pages in pool
  std::string memoryBankName=""; // memory bank to be used. by default, this uses the first memory bank available
};


std::unique_ptr<ReadoutEquipment> getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment> getReadoutEquipmentCruEmulator(ConfigFile &cfg, std::string cfgEntryPoint);
