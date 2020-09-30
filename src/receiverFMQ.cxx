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
// Can also decode messages (e.g. "mode=readout") to check consistency of
// incoming stream

#ifdef WITH_FAIRMQ

#include <signal.h>

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQMessage.h>
#include <fairmq/FairMQTransportFactory.h>
#include <memory>

#include <Common/Configuration.h>
#include <InfoLogger/InfoLogger.hxx>

#include "CounterStats.h"
#include <Common/Timer.h>

#include "RAWDataHeader.h"
#include "SubTimeframe.h"

#include "RdhUtils.h"
#include "DataBlock.h"

// definition of a global for logging
using namespace AliceO2::InfoLogger;
InfoLogger theLog;

// signal handlers
static int ShutdownRequest =
    0; // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int) {
  theLog.log("*** break ***");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest = 1;
}



class ReadoutStfDecoder {
  public:
    ReadoutStfDecoder(std::vector<FairMQMessagePtr> inMsgParts);
    ~ReadoutStfDecoder();
 
    struct Part {
      void *data;
      size_t size;
    };

    std::vector<Part> & getHbf() {    
      return hbf;
    }
    
    double getCopyRatio() {
       return nPartsRepacked*1.0/(nPartsRepacked+nPartsReused);
    }
    
  private:
    std::vector<FairMQMessagePtr> msgParts; // keep ownership of FMQ messages for this object lifetime   
    std::vector<void*> allocatedParts; // keep ownership of data copied
    std::vector<Part> hbf; // list of HBFs
    SubTimeframe *stf = nullptr; // STF header
    
    size_t nPartsReused=0;
    size_t nPartsRepacked=0;
};

ReadoutStfDecoder::ReadoutStfDecoder(std::vector<FairMQMessagePtr> inMsgParts){
  msgParts=std::move(inMsgParts);
  
  // expected format of received messages : (header + superpage + superpage ...)
  // we gonna split in HBF, and if necessary copy HBF data overlapping 2 pages in a newly allocated contiguous block
  
  int i = 0;
  uint32_t lastHbOrbit;
  bool lastHbOrbitUndefined=true;

  std::vector<Part> pendingHbf; // current HBF, completed contiguous parts
  // last chunk of current HBF
  Part lastPart = {nullptr, 0};

  auto pendingHbfCollect = [&](bool isLast) {
    // move last piece, if any
    if (lastPart.size != 0 ) {
      if (lastPart.data==nullptr) {throw;}
      pendingHbf.push_back(lastPart);
    }
    
    if (isLast) {
      // move (if already a single piece) or copy (in a fresh contiguuous block) pending HBF
      if (pendingHbf.size()==1) {
	// use as is
	nPartsReused++;
	hbf.push_back(pendingHbf[0]);
	//printf("HBF 1 page\n");
      } else if (pendingHbf.size()>1) {
      	// create a copy
	size_t size=0;
	for (auto const &p : pendingHbf) {
	  size += p.size;
	}
	char *newHbf = (char *) malloc(size);
	if (newHbf==NULL) {
	  throw;	  
	}
	Part newPart;
	newPart.data=(void *)newHbf;
	newPart.size=size;
	hbf.push_back(newPart);
	allocatedParts.push_back(newHbf);  // keep track for later delete
	for (auto const &p : pendingHbf) {
	  memcpy(newHbf,p.data,p.size);
	  newHbf += p.size;
	  nPartsRepacked++;
	}
      }
      pendingHbf.clear();
    }
    
    // cleanup
    lastPart.data = nullptr;
    lastPart.size = 0;
  };

  for (auto const &mm : msgParts) {
    if (i == 0) {
      // first part is STF header
      if (mm->GetSize() != sizeof(SubTimeframe)) {
        throw;
      }
      stf = (SubTimeframe *)mm->GetData();
    } else {    
      // then 1 part per superpage
      size_t dataSize = mm->GetSize();
      void *data = mm->GetData();
      
      //printf ("Page %d size %d\n",i,(int)dataSize);
      std::string errorDescription;
		    
      for (size_t pageOffset = 0; pageOffset < dataSize;) {
        if (pageOffset + sizeof(o2::Header::RAWDataHeader) > dataSize) {
          throw;
        }

        RdhHandle h(((uint8_t *)data) + pageOffset);
        int nErr = h.validateRdh(errorDescription);

	uint16_t offsetNextPacket = h.getOffsetNextPacket();
	if (offsetNextPacket == 0) {
          throw;
        }
	//h.dumpRdh(pageOffset, 1);
	
	if (lastHbOrbitUndefined || lastHbOrbit != h.getHbOrbit()) {
	  // this is a new HBF
	  //printf("new HBF:%d\n",(int)h.getHbOrbit());
	 
	  // handle previous HBF, now completed
	  pendingHbfCollect(1);

	  // start new HBF	  
	  lastPart.data=((uint8_t *)data) + pageOffset;
	  lastPart.size = offsetNextPacket;
	} else {
	  // check if beginning of new page
	  if (lastPart.data==nullptr) {
	    lastPart.data=((uint8_t *)data) + pageOffset;
	  }
	  lastPart.size += offsetNextPacket;
	}
	lastHbOrbitUndefined = false;
	lastHbOrbit=h.getHbOrbit();
        pageOffset += offsetNextPacket;
      }
      // end of page (but not sure end of HBF, so dont flush it yet)
      pendingHbfCollect(0);
    }
    i++;
  }
  // handle previous HBF, now completed
  pendingHbfCollect(1);   
}

ReadoutStfDecoder::~ReadoutStfDecoder() {
  // release data copied
  for (auto const &p: allocatedParts) {
    free(p);
  }
}


// program main
int main(int argc, const char **argv) {

  ConfigFile cfg;
  const char *cfgFileURI = "";
  std::string cfgEntryPoint = "";
  if (argc < 3) {
    printf("Please provide path to configuration file and entry point (section "
           "name)\n");
    return -1;
  }
  cfgFileURI = argv[1];
  cfgEntryPoint = argv[2];

  // load configuration file
  theLog.log("Reading configuration from %s", cfgFileURI);
  try {
    cfg.load(cfgFileURI);
  } catch (std::string err) {
    theLog.log("Error : %s", err.c_str());
    return -1;
  }

  // configuration parameter: | receiverFMQ | transportType | string | shmem |
  // c.f. parameter with same name in consumer-FairMQchannel-* |
  std::string cfgTransportType = "shmem";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".transportType",
                                    cfgTransportType);

  // configuration parameter: | receiverFMQ | channelName | string | readout |
  // c.f. parameter with same name in consumer-FairMQchannel-* |
  std::string cfgChannelName = "readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelName",
                                    cfgChannelName);

  // configuration parameter: | receiverFMQ | channelType | string | pair | c.f.
  // parameter with same name in consumer-FairMQchannel-* |
  std::string cfgChannelType = "pair";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelType",
                                    cfgChannelType);

  // configuration parameter: | receiverFMQ | channelAddress | string |
  // ipc:///tmp/pipe-readout | c.f. parameter with same name in
  // consumer-FairMQchannel-* |
  std::string cfgChannelAddress = "ipc:///tmp/pipe-readout";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".channelAddress",
                                    cfgChannelAddress);

  // configuration parameter: | receiverFMQ | decodingMode | string | none |
  // Decoding mode of the readout FMQ output stream. Possible values: none (no
  // decoding), stfHbf, stfSuperpage |
  std::string cfgDecodingMode = "none";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".decodingMode",
                                    cfgDecodingMode);
  enum decodingMode { none = 0, stfHbf = 1, stfSuperpage = 2, stfDatablock = 3 };
  decodingMode mode = decodingMode::none;
  if (cfgDecodingMode == "none") {
    mode = decodingMode::none;
  } else if (cfgDecodingMode == "stfHbf") {
    mode = decodingMode::stfHbf;
  } else if (cfgDecodingMode == "stfSuperpage") {
    mode = decodingMode::stfSuperpage;
  } else if (cfgDecodingMode == "stfDatablock") {
    mode = decodingMode::stfDatablock;
  } else {
    theLog.log(InfoLogger::Severity::Error, "Wrong decoding mode set : %s",
               cfgDecodingMode.c_str());
  }

  // configuration parameter: | receiverFMQ | dumpRDH | int | 0 | When
  // set, the RDH of data received are printed (needs decodingMode=readout).|
  int cfgDumpRDH = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".dumpRDH", cfgDumpRDH, 0);

  // configuration parameter: | receiverFMQ | dumpTF | int | 0 | When
  // set, a message is printed when a new timeframe is received.
  // If the value is bigger than one, this specifies a periodic interval between
  // TF print after the first one. (e.g. 100 would print TF 1, 100, 200, etc). |
  int cfgDumpTF = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".dumpTF", cfgDumpTF, 0);
  theLog.log("dumpRDH = %d dumpTF = %d", cfgDumpRDH, cfgDumpTF);

  // create FMQ receiving channel
  theLog.log("Creating FMQ RX channel %s type %s @ %s", cfgChannelName.c_str(),
             cfgChannelType.c_str(), cfgChannelAddress.c_str());
  auto factory =
      FairMQTransportFactory::CreateTransportFactory(cfgTransportType);
  auto pull = FairMQChannel{cfgChannelName, cfgChannelType, factory};
  pull.Connect(cfgChannelAddress);
  // pull.InitCommandInterface();

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings, sizeof(signalSettings));
  signalSettings.sa_handler = signalHandler;
  sigaction(SIGTERM, &signalSettings, NULL);
  sigaction(SIGQUIT, &signalSettings, NULL);
  sigaction(SIGINT, &signalSettings, NULL);

  // init receiving loop
  CounterStats msgStats;
  AliceO2::Common::Timer runningTime;
  int statsTimeout = 1000000;
  runningTime.reset(statsTimeout);
  unsigned long long nMsg = 0;
  unsigned long long nMsgParts = 0;
  unsigned long long nBytes = 0;
  bool isMultiPart = false;

  double copyRatio=0;
  unsigned long long copyRatioCount=0;

  if ( (mode == decodingMode::stfHbf) || (mode == decodingMode::stfSuperpage) || (mode == decodingMode::stfDatablock)){
    isMultiPart = true;
  }

  theLog.log("Entering receiving loop");

  for (; !ShutdownRequest;) {
    auto msg = pull.NewMessage();
    int timeout = 1000;

    if (isMultiPart) {
      std::vector<FairMQMessagePtr> msgParts;
      int64_t bytesReceived;
      bytesReceived = pull.Receive(msgParts, timeout);
      if (bytesReceived > 0) {
        nBytes += bytesReceived;
        nMsg++;
        msgStats.increment(bytesReceived);

        if (mode == decodingMode::stfHbf) {
	
          // expected format of received messages : (header + HB + HB ...)

          int nPart = msgParts.size();
	  nMsgParts += nPart;
          if (nPart < 2) {
            theLog.log(InfoLogger::Severity::Error,
                       "Only %d parts in message, need at least 2", nPart);
            continue;
          }

          int i = 0;
          bool dumpNext = false;
          SubTimeframe *stf = nullptr;
          int numberOfHBF = nPart - 1;
          for (auto const &mm : msgParts) {
            
            if (i == 0) {
              // first part is STF header
              if (mm->GetSize() != sizeof(SubTimeframe)) {
                theLog.log(InfoLogger::Severity::Error,
                           "Header wrong size %d != %d\n", (int)mm->GetSize(),
                           (int)sizeof(SubTimeframe));
                break;
              }
              stf = (SubTimeframe *)mm->GetData();
              if (cfgDumpTF) {
                if ((stf->timeframeId == 1) ||
                    (stf->timeframeId % cfgDumpTF == 0)) {
                  dumpNext = true;
                }
              }
            } else {	  
              if ((numberOfHBF != 0) && (stf->isRdhFormat))  {
	      // then we have 1 part per HBF
              size_t dataSize = mm->GetSize();
              void *data = mm->GetData();
              std::string errorDescription;
              for (size_t pageOffset = 0; pageOffset < dataSize;) {
                if (pageOffset + sizeof(o2::Header::RAWDataHeader) > dataSize) {
                  theLog.log("part %d offset 0x%08lX: not enough space for RDH",
                             i, pageOffset);
                  break;
                }
                RdhHandle h(((uint8_t *)data) + pageOffset);

                if (dumpNext) {
                  printf("Receiving TF %d CRU %d.%d link %d : %d HBf\n",
                         (int)stf->timeframeId, (int)h.getCruId(),
			 (int)h.getEndPointId(),
                         (int)stf->linkId, numberOfHBF);
                  dumpNext = false;
                }

                if (cfgDumpRDH) {
                  h.dumpRdh(pageOffset, 1);
                }

                int nErr = h.validateRdh(errorDescription);
                if (nErr) {
                  if (!cfgDumpRDH) {
                    // dump RDH if not done already
                    h.dumpRdh(pageOffset, 1);
                  }
                  theLog.log(InfoLogger::Severity::Error,
                             "part %d offset 0x%08lX : %s", i, pageOffset,
                             errorDescription.c_str());
                  errorDescription.clear();
                  break;
                }

                // go to next RDH
                uint16_t offsetNextPacket = h.getOffsetNextPacket();
                if (offsetNextPacket == 0) {
                  break;
                }
                pageOffset += offsetNextPacket;
              }
	    } else {
              if (dumpNext) {
                printf("Receiving TF %d link %d\n",
                         (int)stf->timeframeId, (int)stf->linkId);
                dumpNext = false;
              }
	    }
	    }
            i++;
          }
	  
        } else if (mode == decodingMode::stfSuperpage) {

	  nMsgParts += msgParts.size();
          ReadoutStfDecoder decoder(std::move(msgParts));
	  copyRatio+=decoder.getCopyRatio();
	  copyRatioCount++;
	  
	  if (cfgDumpRDH) {
	    int i=0;
	    for (auto const &p : decoder.getHbf()) {
	      printf("HBF %d\n",i);
	      for (size_t offset = 0; offset < p.size;) {
                RdhHandle h(((uint8_t *)p.data) + offset);
                if (cfgDumpRDH) {
                  h.dumpRdh(offset, 1);
        	}
		// go to next RDH
                uint16_t offsetNextPacket = h.getOffsetNextPacket();
                if (offsetNextPacket == 0) {
                  break;
                }
                offset += offsetNextPacket;
	      }
	      i++;
	    }
          }
	} else if (mode == decodingMode::stfDatablock) {
	  nMsgParts += msgParts.size();
	  //printf("parts=%d\n",msgParts.size());
	  if (msgParts.size()!=2) {
	    theLog.log(InfoLogger::Severity::Error,
                       "%d parts in message, should be 2", (int)msgParts.size());
	  } else {
	    int sz = msgParts[0]->GetSize();
	    if (sz != sizeof(DataBlockHeaderBase)) {
              theLog.log(InfoLogger::Severity::Error,
                       "part[0] size = %d, should be %d",sz , (int)sizeof(DataBlock));
	    } else {
	      DataBlockHeaderBase *dbhb = (DataBlockHeaderBase *)msgParts[0]->GetData();
	      // printf("rx datablock size: header %d ?= msgpart %d\n",(int)dbhb->dataSize,(int)msgParts[1]->GetSize());
	    }
	  }
	}
      }
    } else {
      if (pull.Receive(msg, 0) > 0) {
        if (msg->GetSize() == 0) {
          continue;
        }
        msgStats.increment(msg->GetSize());
        nBytes += msg->GetSize();
        nMsg++;
      } else {
        usleep(10000);
      }
    }
    // printf("releasing msg %p\n",msg->GetData());
    // std::cout << " received message of size " << msg->GetSize() << std::endl;
    // // access data via inputMsg->GetData()

    // print regularly the current throughput
    if (runningTime.isTimeout()) {
      double t = runningTime.getTime();
      theLog.log("%.3lf msg/s %.3lf parts/s %.3lfMB/s", nMsg / t, nMsgParts / t,
                 nBytes / (1024.0 * 1024.0 * t));
      if (copyRatioCount) {
        theLog.log("HBF copy ratio = %.3lf %%",copyRatio*100/copyRatioCount);
      }
      runningTime.reset(statsTimeout);
      nMsg = 0;
      nMsgParts = 0;
      nBytes = 0;
    }
  }

  theLog.log("Receiving loop completed");
  theLog.log(
      "bytes received: %llu  (avg=%.2lf  min=%llu  max=%llu  count=%llu)",
      (unsigned long long)msgStats.get(), msgStats.getAverage(),
      (unsigned long long)msgStats.getMinimum(),
      (unsigned long long)msgStats.getMaximum(),
      (unsigned long long)msgStats.getCount());

  return 0;

  // other implementation with a full FMQ device

  std::vector<FairMQChannel> channels(1);
  FairMQDevice fd;

  // todo: check why this type is not public in FMQ interface?
  typedef std::unordered_map<std::string, std::vector<FairMQChannel>> FairMQMap;
  FairMQMap m;

  channels[0].UpdateType(cfgChannelType.c_str()); // pub or push?
  channels[0].UpdateMethod("connect");
  channels[0].UpdateAddress("tcp://localhost:5555");
  channels[0].UpdateRateLogging(0);
  channels[0].UpdateSndBufSize(10);
  if (!channels[0].Validate()) {
    throw "ConsumerFMQ: channel validation failed";
  }

  // todo: def "data-out" as const string to name output channel to which we
  // will push
  m.emplace(std::string("data-in"), channels);

  for (auto it : m) {
    std::cout << it.first << " = " << it.second.size() << " channels  "
              << std::endl;
    for (auto ch : it.second) {
      std::cout << ch.GetAddress() << std::endl;
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
    if (ShutdownRequest)
      break;
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
#include <stdio.h>
int main() {
  printf("Not compiled with FMQ, exiting\n");
  return 0;
}

#endif
