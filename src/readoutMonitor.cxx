// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <Common/Configuration.h>
#include <Common/SimpleLog.h>
#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
#include <map>
#include <signal.h>
#include <zmq.h>
#include "ReadoutStats.h"
#include "SocketRx.h"

// logs in console mode
#include "TtyChecker.h"
TtyChecker theTtyChecker;

#include "ReadoutConst.h"

// definition of a global for logging
using namespace AliceO2::InfoLogger;
InfoLogger theLog;

// signal handlers
static int ShutdownRequest = 0; // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int)
{
  printf("*** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest = 1;
}

/*
// statistics receiving class
class ZmqReadoutMonitorServer : public ZmqServer {
  ZmqReadoutMonitorServer(const std::string address) : ZmqServer(address){
  };
  ~ZmqReadoutMonitorServer(){
  };
  int publish(void* msgBody, int msgSize) {
    printf("rx: %d bytes (%p)\n",msgSize, msgBody);
    return 0;
  }  
};
*/

std::string getStringTime(double timestamp) {
  time_t t=(time_t)timestamp;
  struct tm tm_str;
  localtime_r(&t, &tm_str);
  // double fractionOfSecond = timestamp - t;
  int ix=0;
  const int bufferSize = 64;
  char buffer[bufferSize];  
  ix += strftime(&buffer[ix], bufferSize - ix, "%Y-%m-%d %T", &tm_str);
  // ix += snprintf(&buffer[ix], bufferSize - ix, ".%.3lf", fractionOfSecond);
  if (ix > bufferSize) {
    ix = bufferSize;
  }
  buffer[ix]=0;
  return buffer;
}

// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x) / sizeof(x[0])

std::string NumberOfBytesToString(double value, const char* suffix)
{
  const char* prefixes[] = { " ", "k", "M", "G", "T", "P" };
  int maxPrefixIndex = STATIC_ARRAY_ELEMENT_COUNT(prefixes) - 1;
  int prefixIndex = log(value) / log(1024);
  if (prefixIndex > maxPrefixIndex) {
    prefixIndex = maxPrefixIndex;
  }
  if (prefixIndex < 0) {
    prefixIndex = 0;
  }
  double scaledValue = value / pow(1024, prefixIndex);
  char bufStr[64];
  if (suffix == nullptr) {
    suffix = "";
  }
  // optimize number of digits displayed
  int l = (int)floor(log10(fabs(scaledValue)));
  if (l < 0) {
    l = 3;
  } else if (l <= 3) {
    l = 3 - l;
  } else {
    l = 0;
  }
  snprintf(bufStr, sizeof(bufStr) - 1, "%.*lf %s%s", l, scaledValue, prefixes[prefixIndex], suffix);
  return std::string(bufStr);
}


// program main
int main(int argc, const char** argv)
{

  ConfigFile cfg;
  std::string cfgFileURI = cfgDefaultsPath;
  std::string cfgEntryPoint = "readout-monitor";
  if (argc >= 3) {
    cfgFileURI = argv[1];
    cfgEntryPoint = argv[2];
  }

  theLog.setContext(InfoLoggerContext({ { InfoLoggerContext::FieldName::Facility, (std::string) "readout/monitor" } }));

  // load configuration file
  theLog.log(LogInfoDevel_(3002), "Reading configuration from %s : %s", cfgFileURI.c_str(), cfgEntryPoint.c_str());
  try {
    cfg.load(cfgFileURI);
  } catch (std::string err) {
    theLog.log(LogErrorSupport_(3100), "Error : %s", err.c_str());
    return -1;
  }

  // configuration parameter: | readout-monitor | monitorAddress | string | tcp://127.0.0.1:6008 | Address of the receiving ZeroMQ channel to receive readout statistics. |
  std::string cfgMonitorAddress = "tcp://127.0.0.1:6008";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".monitorAddress", cfgMonitorAddress);

  // output format
  // configuration parameter: | readout-monitor | outputFormat | int | 0 | 0: default, human readable. 1: raw bytes. |
  int cfgRawBytes = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".outputFormat", cfgRawBytes);  
 
  // configuration parameter: | readout-monitor | broadcastPort | int | 0 | when set, the process will create a listening TCP port and broadcast statistics to connected clients. |
  // configuration parameter: | readout-monitor | broadcastHost | string | | used by readout-status to connect to readout-monitor broadcast channel. |
  int cfgBroadcastPort = 0;
  cfg.getOptionalValue<int>(cfgEntryPoint + ".broadcastPort", cfgBroadcastPort);
  std::unique_ptr<SocketRx> broadcastSocket;
  if (cfgBroadcastPort>0) {
    broadcastSocket = std::make_unique<SocketRx>("readoutMonitor", cfgBroadcastPort, &theLog);
  }

  // configuration parameter: | readout-monitor | logFile | string | | when set, the process will log received metrics to a file. |
  // configuration parameter: | readout-monitor | logFileMaxSize | int | 128 | defines the maximum size of log file (in MB). When reaching this threshold, the log file is rotated. |
  // configuration parameter: | readout-monitor | logFileHistory | int | 1 | defines the maximum number of previous log files to keep, when a maximum size is set. |
  bool metricsLogEnabled = 0;
  std::string cfgLogFile = "";
  int cfgLogFileMaxSize = 128;
  int cfgLogFileHistory = 1;
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".logFile", cfgLogFile);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".logFileMaxSize", cfgLogFileMaxSize);
  cfg.getOptionalValue<int>(cfgEntryPoint + ".logFileHistory", cfgLogFileHistory);
  SimpleLog metricsLog;
  if (cfgLogFile != "") {
    if (metricsLog.setLogFile(cfgLogFile.c_str(), cfgLogFileMaxSize * 1024L * 1024L, cfgLogFileHistory, 0) == 0) {
      metricsLogEnabled = 1;
      metricsLog.setOutputFormat(SimpleLog::FormatOption::ShowMessage);
      theLog.log(LogInfoDevel_(3007), "Logging metrics to file %s", cfgLogFile.c_str());
    } else {
      theLog.log(LogWarningDevel_(3232), "Could not create metrics log file %s", cfgLogFile.c_str());
    }
  }
  
  // maximum size of incoming ZMQ messages
  const int cfgMaxMsgSize = sizeof(ReadoutStatsCounters);

  // default ZMQ settings
  int cfg_ZMQ_CONFLATE = 0; // buffer last message only
  int cfg_ZMQ_IO_THREADS = 1; // number of IO threads
  int cfg_ZMQ_LINGER = 1000; // close timeout
  int cfg_ZMQ_RCVTIMEO = 1000; // receive timeout

  theLog.log(LogInfoDevel_(3002), "Creating ZeroMQ server @ %s", cfgMonitorAddress.c_str());

  void* zmqContext = nullptr;
  void* zmqHandle = nullptr;
  void* zmqBuffer = nullptr;
 
  int linerr = 0;
  int zmqerr = 0;
  for (;;) {
    zmqBuffer = malloc(cfgMaxMsgSize);
    if (zmqBuffer == nullptr) {
      linerr = __LINE__;
      break;
    }

    zmqContext = zmq_ctx_new();
    if (zmqContext == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }

    zmq_ctx_set(zmqContext, ZMQ_IO_THREADS, cfg_ZMQ_IO_THREADS);
    if (zmq_ctx_get(zmqContext, ZMQ_IO_THREADS) != cfg_ZMQ_IO_THREADS) {
      linerr = __LINE__;
      break;
    }
    zmqHandle = zmq_socket(zmqContext, ZMQ_PULL);
    if (zmqHandle==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    zmqerr = zmq_setsockopt(zmqHandle, ZMQ_CONFLATE, &cfg_ZMQ_CONFLATE, sizeof(cfg_ZMQ_CONFLATE));
    if (zmqerr) { linerr=__LINE__; break; }
    zmqerr = zmq_setsockopt(zmqHandle, ZMQ_LINGER, (void*)&cfg_ZMQ_LINGER, sizeof(cfg_ZMQ_LINGER));
    if (zmqerr) { linerr=__LINE__; break; }
    zmqerr = zmq_setsockopt(zmqHandle, ZMQ_RCVTIMEO, (void*)&cfg_ZMQ_RCVTIMEO, sizeof(cfg_ZMQ_RCVTIMEO));
    if (zmqerr) { linerr=__LINE__; break; }

    zmqerr = zmq_bind(zmqHandle, cfgMonitorAddress.c_str());
    if (zmqerr) { linerr=__LINE__; break; }

    break;
  }

  if ((zmqerr) || (linerr)) {
    if (zmqerr) {
      theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
    } else {
      theLog.log(LogErrorDevel, "Error @%d", linerr);
    }
    return -1;
  }

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings, sizeof(signalSettings));
  signalSettings.sa_handler = signalHandler;
  sigaction(SIGTERM, &signalSettings, NULL);
  sigaction(SIGQUIT, &signalSettings, NULL);
  sigaction(SIGINT, &signalSettings, NULL);

  theLog.log(LogInfoDevel_(3006), "Entering monitoring loop");

  typedef std::map<std::string, double> MapCountersBySource;
  MapCountersBySource latestUpdate; // keep copy of latest counter for each source

  // header
  if ((!cfgRawBytes)&&(broadcastSocket == nullptr)) {
    printf("               Time    State         nStf   Readout  Recorder      STFB      STFB        STFB      STFB      STFB\n");
    printf("                                              total     total     total    memory      memory    memory       tf \n");
    printf("                                                                           locked     release   release       id \n");
    printf("                                                                                         rate   latency          \n");
    printf("                                            (bytes)   (bytes)   (bytes)    (pages)  (pages/s)       (s)          \n");
  }
    
  for (; !ShutdownRequest;) {
    int nb = 0;
    nb = zmq_recv(zmqHandle, zmqBuffer, cfgMaxMsgSize, 0);
    if (nb < 0) {
      // nothing received
      continue;
    }
    if (nb != sizeof(ReadoutStatsCounters)) {
      // wrong message size
      theLog.log(LogWarningDevel, "ZMQ message: unexpected size %d", nb);
      continue;
    }

    ReadoutStatsCounters* counters = (ReadoutStatsCounters*)zmqBuffer;
    uint64_t state = counters->state.load();
    ((char*)&state)[7] = 0;
    double t = counters->timestamp.load();
    double nRfmq = counters->pagesPendingFairMQreleased.load();
    double avgTfmq = 0.0;

    std::string k = counters->source;
    MapCountersBySource::iterator lb = latestUpdate.lower_bound(k);
    double previousSampleTime = 0;

    if(lb != latestUpdate.end() && !(latestUpdate.key_comp()(k, lb->first))) {
      // source was already sampled
      previousSampleTime = lb->second;
      lb->second = t;
    } else {
      // this is first sample for this source
      latestUpdate.insert(lb, MapCountersBySource::value_type(k, t));
    }

    if (previousSampleTime > 0) {
      double deltaT = t - previousSampleTime;
      nRfmq = nRfmq / deltaT;
      if (nRfmq) {
	avgTfmq = (counters->pagesPendingFairMQtime.load() / nRfmq) / (deltaT * 1000000.0);
      }
      std::string vBufferUsage;
      for (int i = 0; i < ReadoutStatsMaxItems; i++) {
	double r = counters->bufferUsage[i].load();
	vBufferUsage += ( (i==0) ? "" : ",") + ( (r<0) ? "" : std::to_string((int)(r*100)) );
      }
      char buf[1024];
      if (cfgRawBytes) {
        snprintf(buf, sizeof(buf), "%f\t%s\t%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%.2lf\t%.6lf\t%d\t%s\n",
           (double)t,
	   counters->source,
           (char*)&state,
           (unsigned long long)counters->numberOfSubtimeframes.load(),
           (unsigned long long)counters->bytesReadout.load(),
           (unsigned long long)counters->bytesRecorded.load(),
           (unsigned long long)counters->bytesFairMQ.load(),
           (unsigned long long)counters->pagesPendingFairMQ.load(),
           nRfmq,
           avgTfmq,
           (int)counters->timeframeIdFairMQ.load(),
	   vBufferUsage.c_str()
	 );
      } else {
        snprintf(buf, sizeof(buf), "%s  %s %s     %8llu     %s   %s   %s   %6llu    %7.2lf    %6.4lf %8d\n",
           t ? getStringTime(t).c_str() : "-",
	   counters->source,
           (char*)&state,
           (unsigned long long)counters->numberOfSubtimeframes.load(),
           NumberOfBytesToString(counters->bytesReadout.load(),"").c_str(),
           NumberOfBytesToString(counters->bytesRecorded.load(),"").c_str(),
           NumberOfBytesToString(counters->bytesFairMQ.load(),"").c_str(),
           (unsigned long long)counters->pagesPendingFairMQ.load(),
           nRfmq,
           avgTfmq,
	   (int)counters->timeframeIdFairMQ.load());
      }
      
      if (broadcastSocket != nullptr) {
        broadcastSocket->broadcast(buf);
      } else {
        printf("%s",buf);
	fflush(stdout);
      }

      if (metricsLogEnabled) {
        // remove trailing EOL
	int l = strlen(buf);
	if (l) {
	  buf[strlen(buf)-1] = 0;
	}
        metricsLog.info("%s", buf);
      }
    }
    previousSampleTime = t;
  }

  theLog.log(LogInfoDevel_(3006), "Execution completed");

  if (zmqHandle != nullptr) {
    zmq_close(zmqHandle);
    zmqHandle = nullptr;
  }
  if (zmqContext != nullptr) {
    zmq_ctx_destroy(zmqContext);
    zmqContext = nullptr;
  }
  if (zmqBuffer != nullptr) {
    free(zmqBuffer);
    zmqBuffer = nullptr;
  }

  return 0;
}

