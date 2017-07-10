#include <Common/Configuration.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>
#include <Common/Timer.h>

#include <DataFormat/DataBlock.h>
#include <DataFormat/DataBlockContainer.h>
#include <DataFormat/DataSet.h>

#include <memory>


using namespace AliceO2::Common;

class ReadoutEquipment {
  public:
  ReadoutEquipment(ConfigFile &cfg, std::string cfgEntryPoint);
  virtual ~ReadoutEquipment();
  
  DataBlockContainerReference getBlock();

  void start();
  void stop();
  const std::string & getName();

//  protected: 
// todo: give direct access to output FIFO?
  std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> dataOut;

  private:
  std::unique_ptr<Thread> readoutThread;  
  static Thread::CallbackResult  threadCallback(void *arg);
  virtual Thread::CallbackResult  populateFifoOut()=0;  // function called iteratively in dedicated thread to populate FIFO
  AliceO2::Common::Timer clk;
  AliceO2::Common::Timer clk0;
  
  unsigned long long nBlocksOut;
  double readoutRate;
  protected:
  std::string name;
};


std::unique_ptr<ReadoutEquipment> getReadoutEquipmentDummy(ConfigFile &cfg, std::string cfgEntryPoint);
std::unique_ptr<ReadoutEquipment> getReadoutEquipmentRORC(ConfigFile &cfg, std::string cfgEntryPoint);
