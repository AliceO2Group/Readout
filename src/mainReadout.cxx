///
/// @file    mainReadout.cxx
/// @author  Sylvain
///

#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>
#include <DataFormat/DataBlock.h>
#include <DataFormat/DataBlockContainer.h>
#include <DataFormat/MemPool.h>
#include <DataFormat/DataSet.h>

#include <atomic>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <boost/format.hpp>
#include <chrono>
#include <signal.h>

#include <memory>
#include <stdint.h>
  
#include <Common/Timer.h>
#include <Common/Fifo.h>
#include <Common/Thread.h>

#ifdef WITH_DATASAMPLING
#include "DataSampling/InjectorFactory.h"
#endif


#include "ReadoutEquipment.h"
#include "DataBlockAggregator.h"
#include "Consumer.h"


using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;

  
#define LOG_TRACE printf("%d\n",__LINE__);fflush(stdout);


// global entry point to log system
InfoLogger theLog;
  

static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int){
  printf(" *** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest=1;
}











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
      } else {
        theLog.log("Unknown equipment type '%s' for [%s]",cfgEquipmentType.c_str(),kName.c_str());
      }
    }
    catch (std::string errMsg) {
        theLog.log("Failed to configure equipment %s : %s",kName.c_str(),errMsg.c_str());
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
  AliceO2::DataSampling::InjectorInterface *dataSamplingInjector = nullptr;
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


  theLog.log("Starting aggregator");
  agg.start();
  
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
  AliceO2::Common::Timer t0;
  t0.reset(); 


/*
  // reset stats
  unsigned long long nBlocks=0;
  unsigned long long nBytes=0;
  double t1=0.0;
*/


 theLog.log("Entering loop");

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
      if (dataSampling && dataSamplingInjector) {
        dataSamplingInjector->injectSamples(*bc);
      }
#endif

    
      unsigned int nb=(int)bc->size();
      //printf("received 1 vector %p made of %u blocks\n",bc,nb);
      
      
      for (unsigned int i=0;i<nb;i++) {
/*
        printf("pop %d\n",i);
        printf("%p : %d use count\n",(void *)bc->at(i).get(), (int)bc->at(i).use_count());
*/
        DataBlockContainerReference b=bc->at(i);

/*
        nBlocks++;
        nBytes+=b->getData()->header.dataSize;
*/
//        printf("%p : %d use count\n",(void *)b.get(), (int)b.use_count());        
        
//        printf("pushed\n");

	//printf("consuming %p\n",b.get());
        for (auto& c : dataConsumers) {
          c->pushData(b);
        }

       // todo: temporary - for the time being, delete done in FMQ. Replace by shared_ptr
//       delete b;    
//       b.reset();
       //printf("%p : %d use count\n",(void *)b.get(), b.use_count());
        //printf("pop %p\n",(void *)b);

      }
      // todo: check if following needed or not... in principle not as it is a shared_ptr
      // delete bc;
    } else {
      usleep(1000);
    }

  }

  theLog.log("Stopping aggregator");
  agg.stop();


//  t1=t0.getTime();
  
//  theLog.log("Wait a bit");
//  sleep(1);
  theLog.log("Stop consumers");
  
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

#ifdef WITH_DATASAMPLING
  if(dataSamplingInjector) {
    delete dataSamplingInjector;
  }
#endif

/*
  theLog.log("%llu blocks in %.3lf seconds => %.1lf block/s",nBlocks,t1,nBlocks/t1);
  theLog.log("%.1lf MB received",nBytes/(1024.0*1024.0));
  theLog.log("%.3lf MB/s",nBytes/(1024.0*1024.0)/t1);
*/

  theLog.log("Operations completed");

  return 0;

}
