///
/// @file    mainReadout.cxx
/// @author  Sylvain
///

#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>
#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/MemPool.h>
#include <Common/DataSet.h>
#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <signal.h>

#ifdef WITH_DATASAMPLING
#include "DataSampling/InjectorFactory.h"
#endif

#include "ReadoutEquipment.h"
#include "DataBlockAggregator.h"
#include "Consumer.h"
#include "MemoryBankManager.h"
#include "ReadoutUtils.h"


// option to add callgrind instrumentation
// to use: valgrind --tool=callgrind --instr-atstart=no --dump-instr=yes ./a.out
// to display stats: kcachegrind
//#define CALLGRIND
#ifdef CALLGRIND
#include <valgrind/callgrind.h>
#endif


// option to enable compilation with FairMQ support
#ifdef WITH_FAIRMQ
#include <fairmq/FairMQLogger.h>
#include <InfoLogger/InfoLoggerFMQ.hxx>
#endif


// namespace used
using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;


// global entry point to log system
InfoLogger theLog;


// global signal handler to end program
static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int signalId) {
  theLog.log("Received signal %d",signalId);
  printf("*** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest=1;
}

// the main program loop
int main(int argc, char* argv[])
{
  ConfigFile cfg;
  const char* cfgFileURI="";
  if (argc<2) {
    printf("Please provide path to configuration file\n");
    return -1;
  }
  cfgFileURI=argv[1];

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings,sizeof(signalSettings));
  signalSettings.sa_handler=signalHandler;
  sigaction(SIGTERM,&signalSettings,NULL);
  sigaction(SIGQUIT,&signalSettings,NULL);
  sigaction(SIGINT,&signalSettings,NULL);

  // log startup and options
  theLog.log("Readout process starting");
  theLog.log("Optional built features enabled:");
  #ifdef WITH_FAIRMQ
    theLog.log("FAIRMQ : yes");
    // redirect FMQ logs to infologger
    setFMQLogsToInfoLogger(&theLog);
  #else
    theLog.log("FAIRMQ : no");
  #endif

  // load configuration file
  theLog.log("Reading configuration from %s",cfgFileURI);
  try {
    cfg.load(cfgFileURI);
  }
  catch (std::string err) {
    theLog.log("Error : %s",err.c_str());
    return -1;
  }
  

  // extract optional configuration parameters
  double cfgExitTimeout=-1;
  cfg.getOptionalValue<double>("readout.exitTimeout",cfgExitTimeout);
  

  // configuration of memory banks
  for (auto kName : ConfigFileBrowser (&cfg,"bank-")) {
     // skip disabled
    int enabled=1;
    try {
      enabled=cfg.getValue<int>(kName + ".enabled");
    }
    catch (...) {
    }
    if (!enabled) {continue;}

    // bank size    
    std::string cfgSize="";
    cfg.getOptionalValue<std::string>(kName + ".size",cfgSize);
    long long mSize=ReadoutUtils::getNumberOfBytesFromString(cfgSize.c_str());
    if (mSize<=0) {
      theLog.log("Skipping memory bank %s:  wrong size %s",kName.c_str(),cfgSize.c_str());
      continue;
    }

    // bank type
    std::string cfgType="";
    try {
      cfgType=cfg.getValue<std::string>(kName + ".type");
    }
    catch (...) {
      theLog.log("Skipping memory bank %s:  no type specified",kName.c_str());
      continue;
    }
    if (cfgType.length()==0) {continue;}

    // instanciate new memory pool
    theLog.log("Creating memory bank %s: type %s size %lld",kName.c_str(),cfgType.c_str(),mSize);
    std::shared_ptr<MemoryBank> b=nullptr;
    try {
      b=getMemoryBank(mSize, cfgType, kName);
    }
    catch (...) {    
    }
    if (b==nullptr) {
      theLog.log("Failed to create memory bank %s",kName.c_str());
      continue;
    }
    // cleanup the memory range
    b->clear();
    // add bank to list centrally managed
    theMemoryBankManager.addBank(b,kName);
    theLog.log("Bank %s added",kName.c_str());
  }
  
  
  // configuration of data consumers
  std::vector<std::unique_ptr<Consumer>> dataConsumers;
  for (auto kName : ConfigFileBrowser (&cfg,"consumer-")) {

    // skip disabled
    int enabled=1;
    try {
      enabled=cfg.getValue<int>(kName + ".enabled");
    }
    catch (...) {
    }
    if (!enabled) {continue;}

    // instanciate consumer of appropriate type
    std::unique_ptr<Consumer> newConsumer=nullptr;
    try {
      std::string cfgType="";
      cfgType=cfg.getValue<std::string>(kName + ".consumerType");
      theLog.log("Configuring consumer %s: %s",kName.c_str(),cfgType.c_str());

      if (!cfgType.compare("stats")) {
        newConsumer=getUniqueConsumerStats(cfg, kName);
      } else if (!cfgType.compare("FairMQDevice")) {
        #ifdef WITH_FAIRMQ
          newConsumer=getUniqueConsumerFMQ(cfg, kName);
        #else
          theLog.log("Skipping %s: %s - not supported by this build",kName.c_str(),cfgType.c_str());
        #endif
      } else if (!cfgType.compare("FairMQChannel")) {
        #ifdef WITH_FAIRMQ
          newConsumer=getUniqueConsumerFMQchannel(cfg, kName);
        #else
          theLog.log("Skipping %s: %s - not supported by this build",kName.c_str(),cfgType.c_str());
        #endif
      } else if (!cfgType.compare("fileRecorder")) {
        newConsumer=getUniqueConsumerFileRecorder(cfg, kName);
      } else if (!cfgType.compare("checker")) {
        newConsumer=getUniqueConsumerDataChecker(cfg, kName);
      } else {
        theLog.log("Unknown consumer type '%s' for [%s]",cfgType.c_str(),kName.c_str());
      }
    }
    catch (const std::exception& ex) {
        theLog.log("Failed to configure consumer %s : %s",kName.c_str(), ex.what());
        continue;
    }
    catch (...) {
        theLog.log("Failed to configure consumer %s",kName.c_str());
        continue;
    }

    if (newConsumer!=nullptr) {
      dataConsumers.push_back(std::move(newConsumer));
    }

  }


  // configure readout equipments
  int nEquipmentFailures=0; // number of failed equipment instanciation
  std::vector<std::unique_ptr<ReadoutEquipment>> readoutDevices;
  for (auto kName : ConfigFileBrowser (&cfg,"equipment-")) {

    // example iteration on each sub-key
    //for (auto kk : ConfigFileBrowser (&cfg,"",kName)) {
    //  printf("%s -> %s\n",kName.c_str(),kk.c_str());
    //}

    // skip disabled equipments
    int enabled=1;
    cfg.getOptionalValue<int>(kName + ".enabled",enabled);
    if (!enabled) {continue;}

    std::string cfgEquipmentType="";
    cfgEquipmentType=cfg.getValue<std::string>(kName + ".equipmentType");
    theLog.log("Configuring equipment %s: %s",kName.c_str(),cfgEquipmentType.c_str());

    std::unique_ptr<ReadoutEquipment>newDevice=nullptr;
    try {
      if (!cfgEquipmentType.compare("dummy")) {
        newDevice=getReadoutEquipmentDummy(cfg,kName);
      } else if (!cfgEquipmentType.compare("rorc")) {
        newDevice=getReadoutEquipmentRORC(cfg,kName);
      } else if (!cfgEquipmentType.compare("cruEmulator")) {
        newDevice=getReadoutEquipmentCruEmulator(cfg,kName);
      } else {
        theLog.log("Unknown equipment type '%s' for [%s]",cfgEquipmentType.c_str(),kName.c_str());
      }
    }
    catch (std::string errMsg) {
        theLog.log("Failed to configure equipment %s : %s",kName.c_str(),errMsg.c_str());
        nEquipmentFailures++;
        continue;
    }
    catch (int errNo) {
        theLog.log("Failed to configure equipment %s : error #%d",kName.c_str(),errNo);
        nEquipmentFailures++;
        continue;
    }
    catch (...) {
        theLog.log("Failed to configure equipment %s",kName.c_str());
        nEquipmentFailures++;
        continue;
    }

    // add to list of equipments
    if (newDevice!=nullptr) {
      readoutDevices.push_back(std::move(newDevice));
    }
  }

  if (nEquipmentFailures) {
    theLog.log("Some equipments failed to initialize, exiting");
    return -1;
  }


  // aggregator
  theLog.log("Creating aggregator");
  AliceO2::Common::Fifo<DataSetReference> agg_output(1000);
  int nEquipmentsAggregated=0;
  DataBlockAggregator agg(&agg_output,"Aggregator");
  for (auto && readoutDevice : readoutDevices) {
      //theLog.log("Adding equipment: %s",readoutDevice->getName().c_str());
      agg.addInput(readoutDevice->dataOut);
      nEquipmentsAggregated++;
  }
  theLog.log("Aggregator: %d equipments", nEquipmentsAggregated);


  // configuration of data sampling
#ifdef WITH_DATASAMPLING
  int dataSampling=0;
  dataSampling=cfg.getValue<int>("sampling.enabled");
  std::unique_ptr<AliceO2::DataSampling::InjectorInterface> dataSamplingInjector;
  if (dataSampling) {
    theLog.log("Data sampling enabled");
    // todo: create(...) should not need an argument and should get its configuration by itself.
    std::string injector = cfg.getValue<std::string>("sampling.class");
    if(injector=="")
      injector = "MockInjector";
    dataSamplingInjector = AliceO2::DataSampling::InjectorFactory::create(injector);
  } else {
    theLog.log("Data sampling disabled");
  }
  // todo: add time counter to measure how much time is spent waiting for data sampling injection (And other consumers)
#endif


  theLog.log("Starting aggregator");
  agg.start();

  // notify consumers of imminent data flow start
  for (auto& c : dataConsumers) {
    c->starting();
  }

  theLog.log("Starting readout equipments");
  for (auto && readoutDevice : readoutDevices) {
      readoutDevice->start();
  }

  theLog.log("Running");

  // reset exit timeout, if any
  AliceO2::Common::Timer t;
  if (cfgExitTimeout>0) {
    t.reset(cfgExitTimeout*1000000);
    theLog.log("Automatic exit in %.2f seconds",cfgExitTimeout);
  }
  int isRunning=1;

  theLog.log("Entering loop");
  #ifdef CALLGRIND
    theLog.log("Starting callgrind instrumentation");
    CALLGRIND_START_INSTRUMENTATION;
  #endif

  while (1) {
    if (isRunning) {
      if (((cfgExitTimeout>0)&&(t.isTimeout()))||(ShutdownRequest)) {
        isRunning=0;
        theLog.log("Stopping readout");
        for (auto && readoutDevice : readoutDevices) {
          readoutDevice->stop();
        }
        theLog.log("Readout stopped");
        t.reset(1000000);  // add a delay before stopping aggregator - continune to empty FIFOs

        // notify consumers of imminent data flow stop
        for (auto& c : dataConsumers) {
          c->stopping();
        }

      }
    } else {
      if (t.isTimeout()) {
        break;
      }
    }

    DataSetReference bc=nullptr;
    agg_output.pop(bc);


    if (bc!=nullptr) {
      // push to data sampling, if configured
      #ifdef WITH_DATASAMPLING
        if (dataSampling) {
          dataSamplingInjector->injectSamples(bc);
        }
      #endif
      // todo: datasampling can become a consumer, now that consumer interface accepts datasets instead of blocks
      
      for (auto& c : dataConsumers) {
        c->pushData(bc);
      }

    } else {
      usleep(1000);
    }

  }

  #ifdef CALLGRIND
    CALLGRIND_STOP_INSTRUMENTATION;
    CALLGRIND_DUMP_STATS;
    theLog.log("Stopping callgrind instrumentation");
  #endif


  theLog.log("Stopping aggregator");
  agg.stop();

  theLog.log("Stopping consumers");
  // close consumers before closing readout equipments (owner of data blocks)
  dataConsumers.clear();

  agg_output.clear();

  // todo: check nothing in the input pipeline
  // flush & stop equipments
  for (auto && readoutDevice : readoutDevices) {
      // ensure nothing left in output FIFO to allow releasing memory
//      printf("readout: in=%llu  out=%llu\n",readoutDevice->dataOut->getNumberIn(),readoutDevice->dataOut->getNumberOut());
      readoutDevice->dataOut->clear();
  }

//  printf("agg: in=%llu  out=%llu\n",agg_output.getNumberIn(),agg_output.getNumberOut());

  theLog.log("Closing readout devices");
  for (size_t i = 0, size = readoutDevices.size(); i != size; ++i) {
    readoutDevices[i]=nullptr;  // effectively deletes the device
  }
  readoutDevices.clear(); // to do it all in one go

  theLog.log("Operations completed");

  return 0;
}
