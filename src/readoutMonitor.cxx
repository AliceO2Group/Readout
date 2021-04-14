#include <Common/Configuration.h>
#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
#include <signal.h>
#include <zmq.h>
#include "ReadoutStats.h"

// logs in console mode
#include "TtyChecker.h"
TtyChecker theTtyChecker;

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

// program main
int main(int argc, const char** argv)
{

  ConfigFile cfg;
  const char* cfgFileURI = "";
  std::string cfgEntryPoint = "";
  if (argc < 3) {
    printf("Please provide path to configuration file and entry point (section name)\n");
    return -1;
  }
  cfgFileURI = argv[1];
  cfgEntryPoint = argv[2];

  theLog.setContext(InfoLoggerContext({ { InfoLoggerContext::FieldName::Facility, (std::string) "readout/monitor" } }));

  // load configuration file
  theLog.log(LogInfoDevel_(3002), "Reading configuration from %s", cfgFileURI);
  try {
    cfg.load(cfgFileURI);
  } catch (std::string err) {
    theLog.log(LogErrorSupport_(3100), "Error : %s", err.c_str());
    return -1;
  }

  // configuration parameter: | readout-monitor | monitorAddress | string | tcp://127.0.0.1:6008 | Address of the receiving ZeroMQ channel to receive readout statistics. |
  std::string cfgMonitorAddress = "tcp://127.0.0.1:6008";
  cfg.getOptionalValue<std::string>(cfgEntryPoint + ".monitorAddress", cfgMonitorAddress);

  // maximum size of incoming ZMQ messages
  const int cfgMaxMsgSize = sizeof(ReadoutStatsCounters);
  // ZMQ rx timeout in message loop
  const int cfgRxTimeout = 1000;

  theLog.log(LogInfoDevel_(3002), "Creating ZeroMQ server @ %s", cfgMonitorAddress.c_str());

  void* zmqContext = nullptr;
  void* zmqHandle = nullptr;
  void* zmqBuffer = nullptr;

  int zmqError = 0;
  try {
    zmqBuffer = malloc(cfgMaxMsgSize);
    if (zmqBuffer == nullptr) {
      throw __LINE__;
    }

    zmqContext = zmq_ctx_new();
    if (zmqContext == nullptr) {
      zmqError = zmq_errno();
      throw __LINE__;
    }

    zmqHandle = zmq_socket(zmqContext, ZMQ_PULL);
    if (zmqHandle == nullptr) {
      zmqError = zmq_errno();
      throw __LINE__;
    }

    zmqError = zmq_setsockopt(zmqHandle, ZMQ_RCVTIMEO, (void*)&cfgRxTimeout, sizeof(int));
    if (zmqError) {
      throw __LINE__;
    }
    zmqError = zmq_bind(zmqHandle, cfgMonitorAddress.c_str());
    if (zmqError) {
      throw __LINE__;
    }
  } catch (int lineErr) {
    if (zmqError) {
      theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", lineErr, zmqError, zmq_strerror(zmqError));
    } else {
      theLog.log(LogErrorDevel, "Error @%d", lineErr);
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
    time_t t = (time_t)counters->timestamp.load();
    unsigned long long nRfmq = counters->pagesPendingFairMQreleased.load();
    double avgTfmq = 0.0;
    if (nRfmq) {
      avgTfmq = (counters->pagesPendingFairMQtime.load() / nRfmq) / 1000000.0;
    }
    printf("%s\t%s\t%llu\t%llu\t%llu\t%llu\tFMQ\t%llu\t%llu\t%.6lf\n",
           t ? ctime(&t) : "-",
           (char*)&state,
           (unsigned long long)counters->numberOfSubtimeframes.load(),
           (unsigned long long)counters->bytesReadout.load(),
           (unsigned long long)counters->bytesRecorded.load(),
           (unsigned long long)counters->bytesFairMQ.load(),
           (unsigned long long)counters->pagesPendingFairMQ.load(),
           nRfmq,
           avgTfmq);
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
