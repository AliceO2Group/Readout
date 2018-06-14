// Simple data receiver program
// Opens a FMQ receiving channel as described in config file
// Readout messages and print statistics
// Can also decode messages (e.g. "mode=readout") to check consistency of incoming stream


#ifdef WITH_FAIRMQ

#include <signal.h>

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>

#include <InfoLogger/InfoLogger.hxx>
#include <Common/Configuration.h>

#include "CounterStats.h"
#include <Common/Timer.h>

#include "RAWDataHeader.h"
#include "SubTimeframe.h"


// definition of a global for logging
using namespace AliceO2::InfoLogger;
InfoLogger theLog;


// signal handlers
static int ShutdownRequest=0;      // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int){
  theLog.log("*** break ***");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest=1;
}


// Implementation of a simple FMQ device
class FMQReceiver : public FairMQDevice
{
  public:

    FMQReceiver() {
    }
    ~FMQReceiver() {
    }

  protected:

    int msgCount=0;
    int msgBytes=0;

    void Run() override {
       while (CheckCurrentState(RUNNING)) {
         SetTransport("zeromq");
          std::unique_ptr<FairMQMessage> msg(fTransportFactory->CreateMessage());

          if (fChannels.at("data-in").at(0).Receive(msg) > 0) {
            msgBytes+=msg->GetSize();
            msgCount++;
            theLog.log("%d messages, %d bytes",msgCount,msgBytes);

            /*
            uint8_t *payload=(unsigned char*)(msg->GetData());
            for (int k=0;k<16;k++) {
              printf("%02X ",(unsigned int)(payload[k]));
            }
            printf("\n");
            */

            // cout << "Received message: \""
            // << string(static_cast<char *>(msg->GetData()), msg->GetSize())
            // << "\"" << endl;
          } else {
            usleep(200000);
          }
       }
    }
};


// program main
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

  std::string cfgDecodingMode="none";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".decodingMode", cfgDecodingMode);
  enum decodingMode {none=0, readout=1};
  decodingMode mode=decodingMode::none;
  if (cfgDecodingMode=="none") {
    mode=decodingMode::none;
  } else if (cfgDecodingMode=="readout") {
    mode=decodingMode::readout;
  } else {
    theLog.log("Wrong decoding mode set : %s",cfgDecodingMode.c_str());
  }

  // create FMQ receiving channel
  theLog.log("Creating FMQ RX channel %s type %s @ %s",cfgChannelName.c_str(),cfgChannelType.c_str(),cfgChannelAddress.c_str());
  auto factory = FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);
  //pull.InitCommandInterface();

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings,sizeof(signalSettings));
  signalSettings.sa_handler=signalHandler;
  sigaction(SIGTERM,&signalSettings,NULL);
  sigaction(SIGQUIT,&signalSettings,NULL);
  sigaction(SIGINT,&signalSettings,NULL);

  // init receiving loop
  CounterStats msgStats;
  AliceO2::Common::Timer runningTime;
  int statsTimeout=1000000;
  runningTime.reset(statsTimeout);
  int STFpendingMsgs=0;
  int n1,n2;
  unsigned long long nMsg=0;
  unsigned long long nBytes=0;

  theLog.log("Entering receiving loop");

  for (;!ShutdownRequest;){
    auto msg = pull.NewMessage();
    int timeout=1000;
    if (pull.ReceiveAsync(msg)>0) {
      if (msg->GetSize()==0) {continue;}
      msgStats.increment(msg->GetSize());
      nBytes+=msg->GetSize();
      nMsg++;

      //printf("Received message size %d\n",(int)msg->GetSize());

      if (mode==decodingMode::readout) {
        // expected format of received messages : (header + HB + HB ...)
        if (STFpendingMsgs==0) {
          // this should be a header
          if (msg->GetSize()!=sizeof(SubTimeframe)) {
            printf("protocol error! expecting STF header size %d but got %d bytes\n",(int)sizeof(SubTimeframe),(int)msg->GetSize());
            return -1;
          }
          SubTimeframe *stf=(SubTimeframe *)msg->GetData();
          if (stf->timeframeId%100==0) {
            printf("Receiving TF %d link %d : %d HBf\n",(int)stf->timeframeId,(int)stf->linkId,(int)stf->numberOfHBF);
          }
          STFpendingMsgs=stf->numberOfHBF;
          n1=0;
          n2=stf->numberOfHBF;
        } else {
          // this is a HBF
          STFpendingMsgs--;

          if (msg->GetSize()%8192) {
            printf("size not matching expected HBF size (multiple of 8k)\n");
            return -1;
          }
          n1++;
          //printf("received HBF %d/%d\n",n1,n2);

          // iterate from RDH to RDH in received message
          int nBlocks=0;
          char *payload=(char *)msg->GetData();
          for (unsigned int offset=0;offset<msg->GetSize();){
            o2::Header::RAWDataHeader *rdh=(o2::Header::RAWDataHeader *)&(payload[offset]);
            //dumpRDH(rdh);
            offset+=rdh->blockLength;
            nBlocks++;
          }
          //printf("%d CRU blocks in HBF\n",nBlocks);
        }
      } else {
        //usleep(1000);
      }
    } else {
      usleep(10000);
    }
    //printf("releasing msg %p\n",msg->GetData());

    //std::cout << " received message of size " << msg->GetSize() << std::endl; // access data via inputMsg->GetData()

    // print regularly the current throughput
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

  // other implementation with a full FMQ device

  std::vector<FairMQChannel> channels(1);
  FMQReceiver fd;

  // todo: check why this type is not public in FMQ interface?
  typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;
  FairMQMap m;

  channels[0].UpdateType(cfgChannelType.c_str());  // pub or push?
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

  // replacement implementation when FMQ not available
  #include<stdio.h>
  int main() {
    printf("Not compiled with FMQ, exiting\n");
    return 0;
  }

#endif
