#include <Common/Configuration.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>
#include <Common/Timer.h>

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include <memory>


using namespace AliceO2::Common;

class DataBlockAggregator {
  public:
  DataBlockAggregator(AliceO2::Common::Fifo<DataSetReference> *output, std::string name="Aggregator");
  ~DataBlockAggregator();

  int addInput(std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>> input); // add a FIFO to be used as input

  void start(); // starts processing thread
  void stop(int waitStopped=1);  // stop processing thread (and possibly wait it terminates)


  static Thread::CallbackResult  threadCallback(void *arg);

  private:
  std::vector<std::shared_ptr<AliceO2::Common::Fifo<DataBlockContainerReference>>> inputs;
  AliceO2::Common::Fifo<DataSetReference> *output;    //todo: unique_ptr

  std::unique_ptr<Thread> aggregateThread;
  AliceO2::Common::Timer incompletePendingTimer;
  int isIncompletePending;
};
