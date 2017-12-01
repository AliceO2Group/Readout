#ifdef WITH_FAIRMQ

#include <signal.h>

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <fairmq/zeromq/FairMQTransportFactoryZMQ.h>
#include <memory>

#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>

#include "CounterStats.h"
#include <Common/Timer.h>

using namespace AliceO2::InfoLogger;



static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int){
  printf(" *** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest=1;
}





class FMQReceiver : public FairMQDevice
{
  public:

    FMQReceiver() { 
    
    transportFactory = new FairMQTransportFactoryZMQ();
    
    }
    ~FMQReceiver() { 
    
    delete transportFactory;
    }

  protected:   
    
     int msgCount=0;
     int msgBytes=0;
     FairMQTransportFactory *transportFactory;
    
    void Run() override {
       while (CheckCurrentState(RUNNING)) {

          std::unique_ptr<FairMQMessage> msg(transportFactory->CreateMessage());

          if (fChannels.at("data-in").at(0).Receive(msg) > 0) {
            msgBytes+=msg->GetSize();
            msgCount++;
            printf("%d messages, %d bytes\n",msgCount,msgBytes);
      //      cout << "Received message: \""
      //      << string(static_cast<char *>(msg->GetData()), msg->GetSize())
      //      << "\"" << endl;
          } else {
            usleep(200000);
          }
       }
    }
};


// receive FMQ messages in a channel (without FMQ device)
void channelOnlyReceiver() {
  auto factory = FairMQTransportFactory::CreateTransportFactory("zeromq");
  auto pull = FairMQChannel{"data-in", "sub", factory};
  pull.Connect("tcp://localhost:5555");
  printf("connect done\n");
  for (;;){
  auto msg = pull.NewMessage();
  pull.Receive(msg);

  std::cout << " received message of size " << msg->GetSize() << std::endl; // access data via inputMsg->GetData()
}
  return;
}



InfoLogger theLog;


int main(int argc, const char **argv) {

  ConfigFile cfg;
  const char* cfgFileURI="";
  std::string cfgEntryPoint="";
  if (argc<3) {
    printf("Please provide path to configuration file and entry point (section name)\n");
    return -1;
  }
  cfgFileURI=argv[1];
  cfgEntryPoint=argv[2];

  // load configuration file
  theLog.log("Reading configuration from %s",cfgFileURI);
  try {
    cfg.load(cfgFileURI);
  }
  catch (std::string err) {
    theLog.log("Error : %s",err.c_str());
    return -1;
  }

  std::string cfgTransportType="shmem";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".transportType", cfgTransportType);

  std::string cfgChannelName="readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelName", cfgChannelName);

  std::string cfgChannelType="pair";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelType", cfgChannelType);

  std::string cfgChannelAddress="ipc:///tmp/pipe-readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelAddress", cfgChannelAddress);

  theLog.log("Creating FMQ RX channel %s type %s @ %s",cfgChannelName.c_str(),cfgChannelType.c_str(),cfgChannelAddress.c_str());

  auto factory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);
  printf("connect done\n");
  
  
  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings,sizeof(signalSettings));
  signalSettings.sa_handler=signalHandler;
  sigaction(SIGTERM,&signalSettings,NULL);
  sigaction(SIGQUIT,&signalSettings,NULL);
  sigaction(SIGINT,&signalSettings,NULL);

  theLog.log("Entering receiving loop");
  CounterStats msgStats;
  AliceO2::Common::Timer runningTime;
  int statsTimeout=1000000;
  runningTime.reset(statsTimeout);
  
  unsigned long long nMsg=0;
  unsigned long long nBytes=0;
  for (;!ShutdownRequest;){
    auto msg = pull.NewMessage();
    pull.Receive(msg);
    if (msg->GetSize()==0) {continue;}
    msgStats.increment(msg->GetSize());
    nBytes+=msg->GetSize();
    nMsg++;
    /*
    for (int k=0;k<10;k++) {
      printf("%d ",(int)(((char*)(msg->GetData()))[k]));
    }
    printf("\n");
    */
    //std::cout << " received message of size " << msg->GetSize() << std::endl; // access data via inputMsg->GetData()
    if (runningTime.isTimeout()) {
      double t=runningTime.getTime();
      theLog.log("%.3lf msg/s %.3lfMB/s",nMsg/t,nBytes/(1024.0*1024.0*t));
      runningTime.reset(statsTimeout);
      nMsg=0;
      nBytes=0;
    }
  }

  theLog.log("Receiving loop completed");
  theLog.log("bytes received: %lu  (avg=%.2lf  min=%lu  max=%lu  count=%lu)",msgStats.get(),msgStats.getAverage(),msgStats.getMinimum(),msgStats.getMaximum(),msgStats.getCount());

  return 0;

   
   
   
   std::vector<FairMQChannel> channels(1);
   FMQReceiver fd;

// todo: check why this type is not public in FMQ interface?  
     typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;   
    FairMQMap m;
    




       
    channels[0].UpdateType("sub");  // pub or push?
    channels[0].UpdateMethod("connect");
    channels[0].UpdateAddress("tcp://localhost:5555");
    channels[0].UpdateRateLogging(0);    
    channels[0].UpdateSndBufSize(10);    
    if (!channels[0].ValidateChannel()) {
      throw "ConsumerFMQ: channel validation failed";
    }


    // todo: def "data-out" as const string to name output channel to which we will push
    m.emplace(std::string("data-in"),channels);
    
    for (auto it : m) {
      std::cout << it.first << " = " << it.second.size() << " channels  " << std::endl;
      for (auto ch : it.second) {
        std::cout << ch.GetAddress() <<std::endl;
      }
    }
      
    fd.fChannels = m;
    fd.SetTransport("zeromq");
    fd.ChangeState(FairMQStateMachine::Event::INIT_DEVICE);
    fd.WaitForEndOfState(FairMQStateMachine::Event::INIT_DEVICE);
    fd.ChangeState(FairMQStateMachine::Event::INIT_TASK);
    fd.WaitForEndOfState(FairMQStateMachine::Event::INIT_TASK);
    fd.ChangeState(FairMQStateMachine::Event::RUN);

//    fd.InteractiveStateLoop();
  
  
    for (;;) {
      if (ShutdownRequest) break;
      sleep(1);
    }
    printf("Exit requested\n");  
  

    fd.ChangeState(FairMQStateMachine::Event::STOP);
    fd.ChangeState(FairMQStateMachine::Event::RESET_TASK);
    fd.WaitForEndOfState(FairMQStateMachine::Event::RESET_TASK);
    fd.ChangeState(FairMQStateMachine::Event::RESET_DEVICE);
    fd.WaitForEndOfState(FairMQStateMachine::Event::RESET_DEVICE);
    fd.ChangeState(FairMQStateMachine::Event::END);


    printf("Done!\n");
  return 0;
}

#else
#include<stdio.h>
int main() {

  printf("Not compiled with FMQ, exiting\n");
  return 0;
}
#endif
