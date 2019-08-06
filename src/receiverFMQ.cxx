// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

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
       while (!NewStatePending()) {
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

  // configuration parameter: | receiverFMQ | transportType | string | shmem | c.f. parameter with same name in consumer-FMQchannel-* |
  std::string cfgTransportType="shmem";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".transportType", cfgTransportType);

  // configuration parameter: | receiverFMQ | channelName | string | readout | c.f. parameter with same name in consumer-FMQchannel-* |
  std::string cfgChannelName="readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelName", cfgChannelName);

  // configuration parameter: | receiverFMQ | channelType | string | pair | c.f. parameter with same name in consumer-FMQchannel-* |
  std::string cfgChannelType="pair";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelType", cfgChannelType);

  // configuration parameter: | receiverFMQ | channelAddress | string | ipc:///tmp/pipe-readout | c.f. parameter with same name in consumer-FMQchannel-* |
  std::string cfgChannelAddress="ipc:///tmp/pipe-readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelAddress", cfgChannelAddress);

  // configuration parameter: | receiverFMQ | decodingMode | string | none | Decoding mode of the readout FMQ output stream. Possible values: none (no decoding), readout (wp5 protocol) |
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
  unsigned long long nMsg=0;
  unsigned long long nBytes=0;
  bool isMultiPart=false;
  
  if (mode==decodingMode::readout) {
    isMultiPart=true;
  }
  
  theLog.log("Entering receiving loop");

  for (;!ShutdownRequest;){
    auto msg = pull.NewMessage();
    int timeout=1000;
    
    if (isMultiPart) {
      FairMQParts msgParts;
      int64_t bytesReceived;
      bytesReceived=pull.Receive(msgParts, timeout);
      if (bytesReceived>0) {
        nBytes+=bytesReceived;
        nMsg++;
	msgStats.increment(bytesReceived);
	
	if (mode==decodingMode::readout) {
	
	  int nPart=msgParts.Size();
	  //printf("msg: %d parts\n",nPart);
	  if (nPart<2) {
	    printf("Error : %d parts in message\n",nPart);
	    continue;
	  }
	  	  
	  // expected format of received messages : (header + HB + HB ...)
	  
	  // first part is STF header
	  if (msgParts[0].GetSize()!=sizeof(SubTimeframe)) {
	    printf("Error : header wrong size %d != %d\n",(int)msgParts[0].GetSize(),(int)sizeof(SubTimeframe));
	    continue;
	  } 	  	  	  
          SubTimeframe *stf=(SubTimeframe *)msgParts[0].GetData();
	  if (stf->numberOfHBF!=nPart-1) {
	    printf("Mismatch stf->numberOfHBF %d != %d message parts - 1\n",stf->numberOfHBF,nPart-1); 
	    continue;
	  }
	  
	  // number of message parts matches number of HBFs in header ?
	  const int printStep=100;
          if ((stf->timeframeId==1)||(stf->timeframeId%printStep==0)) {
            printf("Receiving TF %d link %d : %d HBf\n",(int)stf->timeframeId,(int)stf->linkId,(int)stf->numberOfHBF);
          }
	  
	  for(int i=1;i<nPart;i++) {	  
            if (msgParts[i].GetSize()%8192) {
              printf("size not matching expected HBF size (multiple of 8k)\n");
              break;
            }

            /*// iterate from RDH to RDH in received message
            int nBlocks=0;
            char *payload=(char *)msgParts[i].GetData();
            for (unsigned int offset=0;offset<msgParts[i].GetSize();){
              o2::Header::RAWDataHeader *rdh=(o2::Header::RAWDataHeader *)&(payload[offset]);
              //dumpRDH(rdh);
              offset+=rdh->blockLength;
              nBlocks++;
            }
            //printf("%d CRU blocks in HBF\n",nBlocks);
	    */
          }
	}
      }
    } else {
      if (pull.Receive(msg,0)>0) {
	if (msg->GetSize()==0) {continue;}
	msgStats.increment(msg->GetSize());
	nBytes+=msg->GetSize();
	nMsg++;
      } else {
	usleep(10000);
      }
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
  theLog.log("bytes received: %llu  (avg=%.2lf  min=%llu  max=%llu  count=%llu)",msgStats.get(),msgStats.getAverage(),msgStats.getMinimum(),msgStats.getMaximum(),msgStats.getCount());

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
  fd.ChangeState(fair::mq::Transition::InitDevice);
  fd.WaitForState(fair::mq::State::InitializingDevice);
  fd.ChangeState(fair::mq::Transition::CompleteInit);
  fd.WaitForState(fair::mq::State::Initialized);
  fd.ChangeState(fair::mq::Transition::Bind);
  fd.WaitForState(fair::mq::State::Bound);
  fd.ChangeState(fair::mq::Transition::Connect);
  fd.WaitForState(fair::mq::State::DeviceReady);
  fd.ChangeState(fair::mq::Transition::InitTask);
  fd.WaitForState(fair::mq::State::Ready);
  fd.ChangeState(fair::mq::Transition::Run);

//    fd.InteractiveStateLoop();

  for (;;) {
    if (ShutdownRequest) break;
    sleep(1);
  }
  printf("Exit requested\n");

  fd.ChangeState(fair::mq::Transition::Stop);
  fd.WaitForState(fair::mq::State::Ready);
  fd.ChangeState(fair::mq::Transition::ResetTask);
  fd.WaitForState(fair::mq::State::DeviceReady);
  fd.ChangeState(fair::mq::Transition::ResetDevice);
  fd.WaitForState(fair::mq::State::Idle);
  fd.ChangeState(fair::mq::Transition::End);

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
