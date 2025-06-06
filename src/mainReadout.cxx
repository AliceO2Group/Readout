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

///
/// @file    mainReadout.cxx
/// @author  Sylvain
///

#include <Common/Configuration.h>
#include <Common/Fifo.h>
#include <Common/LineBuffer.h>
#include <Common/MemPool.h>
#include <Common/Thread.h>
#include <Common/Timer.h>
#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
#include <boost/property_tree/ptree.hpp>
#include <thread>
#include <time.h>
#include <string.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <charconv>
#include <atomic>
#include <filesystem>

#include "DataBlock.h"
#include "DataBlockContainer.h"
#include "DataSet.h"
#include "readoutErrorCodes.h"

#ifdef WITH_ZMQ
#include "ZmqServer.hxx"
#endif

#ifdef WITH_CONFIG
#include <Configuration/ConfigurationFactory.h>
#endif

#ifdef WITH_LOGBOOK
#include <BookkeepingApi/BkpClientFactory.h>
#endif

#ifdef WITH_DB
#include "ReadoutDatabase.h"
#endif

#ifdef WITH_READOUTCARD
#include <ReadoutCard/Version.h>
#endif

#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <map>
#include <memory>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "Consumer.h"
#include "DataBlockAggregator.h"
#include "MemoryBankManager.h"
#include "ReadoutEquipment.h"
#include "ReadoutStats.h"
#include "ReadoutUtils.h"
#include "ReadoutVersion.h"
#include "TtyChecker.h"
#include "ReadoutConst.h"
#include "ReadoutMonitoringQueue.h"

#ifdef WITH_NUMA
#include <numa.h>
#endif

// option to add callgrind instrumentation
// to use: valgrind --tool=callgrind --instr-atstart=no --dump-instr=yes ./a.out
// to display stats: kcachegrind
//#define CALLGRIND
#ifdef CALLGRIND
#include <valgrind/callgrind.h>
#endif

#ifdef WITH_GPERFTOOLS
#include <gperftools/profiler.h>
#endif

// option to enable compilation with FairMQ support
#ifdef WITH_FAIRMQ
#include <InfoLogger/InfoLoggerFMQ.hxx>
#include <fairmq/FairMQLogger.h>
#endif

// option to enable compilation with Control-OCCPlugin support
//#define WITH_OCC
//#undef WITH_OCC
#ifdef WITH_OCC
#include <OccInstance.h>
#include <RuntimeControlledObject.h>
#else
#define OCC_CONTROL_PORT_ENV ""
#define OCC_ROLE_ENV ""
#endif

// namespace used
using namespace AliceO2::InfoLogger;
using namespace AliceO2::Common;

// some constants
const char* envRunNumber = "O2_RUN"; // env var name for run number store
const char* gperfOutputFile = "/tmp/readout.gperf"; // path to output file when gperf profiling enabled

// set log environment before theLog is initialized
TtyChecker theTtyChecker;

// global entry point to log system
InfoLogger theLog;
InfoLoggerContext theLogContext;

// global signal handler to end program
static int ShutdownRequest = 0; // set to 1 to request termination, e.g. on SIGTERM/SIGQUIT signals
static void signalHandler(int signalId)
{
  theLog.log(LogInfoDevel, "Received signal %d", signalId);
  printf("*** break ***\n");
  if (ShutdownRequest) {
    // immediate exit if pending exit request
    exit(1);
  }
  ShutdownRequest = 1;
}

// some globals needed in other components
std::string occRole;     // OCC role name
tRunNumber occRunNumber = undefinedRunNumber; // OCC run number

// a general purpose log function for DB
void dbLog(const std::string &msg) {
  theLog.log(LogInfoDevel_(3012), "%s", msg.c_str());
}

// an object running a thread redirecting from a file descriptor to infologger
// the file descriptor should be valid for the lifetime of the object
class TheLogRedirection {
  public:
    TheLogRedirection(int fd, std::string name, int errcode) {
      this->fd = fd;
      this->name = name;
      this->errcode = errcode;
      shutdownRequest = 0;
      std::function<void(void)> f = std::bind(&TheLogRedirection::run, this);
      th = std::make_unique<std::thread>(f);
    };
   ~TheLogRedirection() {
     shutdownRequest = 1;
     if (th != nullptr) {
       th->join();
       th = nullptr;
     }
   }
  private:
    int fd = -1; // the file descriptor to monitor
    std::string name; // name used as header for the output logs
    int errcode = 0; // an error code to be used for output messages
    std::unique_ptr<std::thread> th; // a thread reading from fd and injecting to theLog
    std::atomic<int> shutdownRequest; // flag to terminate thread
    void run() {
      setThreadName((std::string("log-") + name).c_str());
      if (fd>=0) {
        // thread loop
        LineBuffer b;
        while (!shutdownRequest) {
          std::string result;
          if (b.appendFromFileDescriptor(fd, 100)) {
            break;
          }
          while (!b.getNextLine(result)) {
            theLog.log(LogInfoDevel_(errcode), "%s: %s", name.c_str(), result.c_str());
          }
        }
      }
      theLog.log(LogInfoDevel_(errcode),"%s: logs completed", name.c_str());
    }
};

// an object running a thread to publish data to logbook
// this allows isolating blocking calls to the bookkeeping api
#ifdef WITH_LOGBOOK
class LogbookThread {
  public:
    LogbookThread(std::unique_ptr<o2::bkp::api::BkpClient> h) {
      this->logbookHandle = std::move(h);
      shutdownRequest = 0;
      publishRequest = 0;
      std::function<void(void)> f = std::bind(&LogbookThread::run, this);
      th = std::make_unique<std::thread>(f);
    };
   ~LogbookThread() {
     shutdownRequest = 1;
     if (th != nullptr) {
       th->join();
       th = nullptr;
     }
   };
   // when timeout is set, ensures the function returns only when bookkeeping operation completed
   // otherwise, it is done asynchronously later
   // returns 0 on success, or an error code
   int publishStats(int timeoutMilliseconds = 0) {
     if (logbookHandle == nullptr) return __LINE__; // fail if no connection
     AliceO2::Common::Timer timer;
     if (timeoutMilliseconds > 0) {
       // wait pending request completed, if any (so that we are sure to push the latest counters)
       timer.reset(timeoutMilliseconds * 1000);
       while (publishRequest.load()) {
         if (timer.isTimeout()) return __LINE__;
         usleep(1000);
       }
     }
     if (publishRequest.load()) return __LINE__; // fail if request already pending
     publishRequest = 1;
     if (this->verbose) {
       theLog.log(LogInfoDevel_(3210), "Requested to publish bookkeeping stats");
     }
     if (timeoutMilliseconds > 0) {
       // wait request completed and check status
       while (publishRequest.load()) {
         if (timer.isTimeout()) return __LINE__;
         usleep(1000);
       }
       if (!publishSuccess.load()) {
         return __LINE__;
       }
     }
     return 0;
   };
   bool verbose = 0; // flag for extra logs on request/publish
  private:
    std::unique_ptr<o2::bkp::api::BkpClient> logbookHandle; // handle to logbook
    std::unique_ptr<std::thread> th; // a thread reading from fd and injecting to theLog
    std::atomic<int> shutdownRequest; // flag to terminate thread
    std::atomic<int> publishRequest; // flag to ask thread to publish current values
    std::atomic<int> publishSuccess; // flag to report status of latest publish operation
    void run() {
      int nSuccessiveFailures = 0;
      const int maxSuccessiveFailures = 3;
      setThreadName("logbook");
      // thread loop, 10Hz
      while (!shutdownRequest && (logbookHandle != nullptr)) {
        if (publishRequest.load() == 1) {
          publishSuccess = 0;
          // copy current counters
          ReadoutStatsCounters snapshot;
          memcpy((void *)&snapshot, (void *)&gReadoutStats.counters, sizeof(snapshot));
          // publishing to logbook makes sense only if a run number defined
          if (snapshot.runNumber.load() != undefinedRunNumber) {
            try {
              // interface: https://github.com/AliceO2Group/Bookkeeping/tree/main/cxx-client/include/BookkeepingApi
              logbookHandle->flp()->updateReadoutCountersByFlpNameAndRunNumber(
                snapshot.source, snapshot.runNumber.load(),
                (int64_t)snapshot.numberOfSubtimeframes.load(), (int64_t)snapshot.bytesReadout.load(), (int64_t)snapshot.bytesRecorded.load(), (int64_t)snapshot.bytesFairMQ.load()
              );
              if (this->verbose) {
                theLog.log(LogInfoDevel_(3210), "Publishing bookkeeping stats: tf = %llu, bytesReadout = %llu", (unsigned long long)snapshot.numberOfSubtimeframes.load(), (unsigned long long)snapshot.bytesReadout.load());
              }
              publishSuccess = 1;
            } catch (const std::exception& ex) {
              theLog.log(LogErrorDevel_(3210), "Failed to update bookkeeping: %s", ex.what());
            } catch (...) {
              theLog.log(LogErrorDevel_(3210), "Failed to update bookkeeping: unknown exception");
            }
            if (!publishSuccess.load()) {
              nSuccessiveFailures++;
	      if (nSuccessiveFailures >= maxSuccessiveFailures) {
                // closing logbook immediately
                logbookHandle = nullptr;
                theLog.log(LogErrorSupport_(3210), "Bookkeeping updates now disabled, after %d consecutive failures", nSuccessiveFailures);
                break;
              }
            } else {
	      nSuccessiveFailures = 0;
	    }
          }
          publishRequest = 0;
        }
        usleep(100000);
      }
    }
};
#endif

class Readout
{

 public:
  ~Readout();

  // protected calls catching all exceptions
  int init(int argc, char* argv[]) {
    try {
      return _init(argc, argv);
    }
    catch (const std::exception& e) {
      theLog.log(LogErrorSupport_(3245), "Exception : %s", e.what());
    }
    return -1;
  }

  // Wrapper function to catch exception and report fatal errors
  int executeFunction(std::string actionName, std::function<int()> f, int plusZeroIsFatal = 1, int minusZeroIsFatal = 1) {
    // theLog.log(LogDebugDevel, "Calling function for %s",actionName.c_str());
    int err = -1;
    try {
      err = f();
    }
    catch (const std::exception& e) {
      theLog.log(LogErrorSupport_(3245), "Exception : %s", e.what());
    }
    if ( ((err<0)&&(minusZeroIsFatal)) || ((err>0)&&(plusZeroIsFatal)) ) {
      std::vector<std::string> m;
      theLog.historyGetSummary(m);
      std::string reason = "Readout failed in " + actionName + ". Please check previous messages.";
      if (m.size()) {
        reason += " First error logged was " + m[0];
      }
      theLog.log(LogFatalOps, "%s", reason.c_str());
    }
    return err;
  }

  int configure(const boost::property_tree::ptree& properties) {
    theLog.historyReset(1);
    return executeFunction("CONFIGURE", std::bind(&Readout::_configure, this, properties));
  }
  int reset() { // as opposed to configure()
    theLog.historyReset(1);
    return executeFunction("RESET", std::bind(&Readout::_reset, this));
  }
  int start() {
    theLog.historyReset(1);
    return executeFunction("START", std::bind(&Readout::_start, this));
  }
  int stop() { // as opposed to start()
    theLog.historyReset(1);
    return executeFunction("STOP", std::bind(&Readout::_stop, this));
  }
  int iterateRunning() {
    return executeFunction("RUNNING", std::bind(&Readout::_iterateRunning, this), 0, 1); // special handling of positive error code: not fatal
  }
  int iterateCheck() {
    return executeFunction("CHECK", std::bind(&Readout::_iterateCheck, this));
  }

  void loopRunning(); // called in state "running"

  bool standaloneMode = false; // flag set when readout running in standalone mode (auto state machines)
  int cfgTimeStart = 0;        // time at which START should be executed in standalone mode
  int cfgTimeStop = 0;         // time at which STOP should be executed in standalone mode
  int cfgNumberOfRuns = 1;           // number of START/STOP loops to execute

 private:
  int _init(int argc, char* argv[]);
  int _configure(const boost::property_tree::ptree& properties);
  int _reset(); // as opposed to configure()
  int _start();
  int _stop(); // as opposed to start()
  int _iterateRunning();
  int _iterateCheck();

  ConfigFile cfg;
  const char* cfgFileURI = "";
  const char* cfgFileEntryPoint = ""; // where in the config tree to look for

  // configuration parameters
  double cfgExitTimeout;
  double cfgFlushEquipmentTimeout;
  double cfgFlushConsumerTimeout;
  int cfgDisableTimeframes;
  int cfgDisableAggregatorSlicing;
  double cfgAggregatorSliceTimeout;
  double cfgAggregatorStfTimeout;
  double cfgTfRateLimit;
  int cfgTfRateLimitMode;
  int cfgLogbookEnabled;
  std::string cfgLogbookUrl;
  std::string cfgLogbookApiToken;
  int cfgLogbookUpdateInterval;
  std::string cfgDatabaseCxParams;
  std::string cfgTimeframeServerUrl;
  std::string cfgExternalSyncServer;
  int cfgExternalSyncTimeout;
  int cfgVerbose = 0;
  int cfgMaxMsgError; // maximum number of error messages before stopping run
  int cfgMaxMsgWarning; // maximum number of warning messages before stopping run
  int cfgCustomCommandsEnabled = 0; // when set, a sub-process bash is launched to execute custom commands
  std::string cfgCustomCommandsShell = "/bin/sh o2-readout-command-launcher";
  std::map<std::string,std::string> customCommands; // map of state / command pairs to be executed
  pid_t customCommandsShellPid = 0; // pid of shell for custom commands
  int customCommandsShellFdIn = -1; // input to shell
  int customCommandsShellFdOut = -1; // output from shell
  int customCommandsShellFdErr = -1; // err from shell
  std::unique_ptr<TheLogRedirection> customCommandsShellLog; // thread redirecting shell stderr to infologger
  void executeCustomCommand(const char *stateChange); // execute a custom command for given state transition, if any

  // runtime entities
  std::vector<std::unique_ptr<Consumer>> dataConsumers;
  std::map<Consumer*, std::string> consumersOutput; // for the consumers having an output, keep a reference
                                                    // to the consumer and the name of the consumer to which
                                                    // to push data
  std::vector<std::unique_ptr<ReadoutEquipment>> readoutDevices;
  std::unique_ptr<DataBlockAggregator> agg;
  std::unique_ptr<AliceO2::Common::Fifo<DataSetReference>> agg_output;

  int isRunning = 0;                          // set to 1 when running, 0 when not running (or should stop running)
  AliceO2::Common::Timer startTimer;          // time counter from start()
  AliceO2::Common::Timer stopTimer;           // time counter from stop()
  std::unique_ptr<std::thread> runningThread; // the thread active in "running" state

  int latencyFd = -1; // file descriptor for the "deep sleep" disabled

  bool isError = 0;                   // flag set to 1 when error has been detected
  bool logFirstError = 0;             // flag set to 1 after 1 error reported from iterateCheck/iterateRunning procedures

#ifdef WITH_LOGBOOK
  std::unique_ptr<LogbookThread> theLogbookThread; // handle to logbook
#endif

#ifdef WITH_DB
  std::unique_ptr<ReadoutDatabase> dbHandle; // handle to readout database
#endif

  void publishLogbookStats(int timeout = 0);          // publish current readout counters to logbook. Optional timeout in milliseconds, async op if not set.
  AliceO2::Common::Timer logbookTimer; // timer to handle readout logbook publish interval

  uint64_t currentTimeframeId = undefinedTimeframeId;
  uint64_t countTimeframeId;

#ifdef WITH_ZMQ
  std::unique_ptr<ZmqServer> tfServer;
#endif
};

bool testLogbook = false; // flag for logbook test mode

void Readout::publishLogbookStats(int timeout)
{
  //  gReadoutStats.print();

#ifdef WITH_DB
  if (dbHandle != nullptr) {
    dbHandle->updateRunCounters(
      (int64_t)gReadoutStats.counters.numberOfSubtimeframes,
      (int64_t)gReadoutStats.counters.bytesReadout,
      (int64_t)gReadoutStats.counters.bytesRecorded,
      (int64_t)gReadoutStats.counters.bytesFairMQ
    );
  }
#endif

#ifdef WITH_LOGBOOK
  if (theLogbookThread != nullptr) {
    int err = theLogbookThread->publishStats(timeout);
    if ((timeout > 0) && (err)) {
      theLog.log(LogErrorDevel_(3210), "Bookkeeping publish failed within given time (%d ms)", timeout);
    }
  }
#endif
}

int Readout::_init(int argc, char* argv[])
{
  setThreadName(nullptr);
    
  int doMemLock = 0; // when set, ensure all allocated memory is locked in ram
  std::string readoutExe = ""; // when set, use specified executable
  std::string readoutConfig = ""; // when set, use specified executable

  // cache of logs - delay startup messages
  std::vector<std::pair<AliceO2::InfoLogger::InfoLogger::InfoLoggerMessageOption, std::string>> initLogs;

  // load configuration defaults
  ConfigFile cfgDefaults;
  const std::string cfgDefaultsEntryPoint = "readout"; // entry point for default configuration variables (e.g. section named [readout])
  std::string cfgStatsPublishAddress; // address where to publish readout stats, eg "tcp://127.0.0.1:6008"
  double cfgStatsPublishInterval = 5.0; // interval for readout stats publish, in seconds
  std::string cfgMembanksMonitorPath = ""; // path to memory banks monitor output
  double cfgMembanksMonitorRate = 0.0; // rate of memory banks monitor updates
  try {
    cfgDefaults.load(cfgDefaultsPath.c_str());
    initLogs.push_back({LogInfoDevel, "Defaults loaded from " + cfgDefaultsPath});
    cfgDefaults.getOptionalValue<int>(cfgDefaultsEntryPoint + ".memLock", doMemLock, doMemLock);
    cfgDefaults.getOptionalValue<std::string>(cfgDefaultsEntryPoint + ".readoutExe", readoutExe, readoutExe);
    cfgDefaults.getOptionalValue<std::string>(cfgDefaultsEntryPoint + ".readoutConfig", readoutConfig, readoutConfig);
    cfgDefaults.getOptionalValue<int>(cfgDefaultsEntryPoint + ".verbose", cfgVerbose, cfgVerbose);
    cfgDefaults.getOptionalValue<std::string>(cfgDefaultsEntryPoint + ".statsPublishAddress", cfgStatsPublishAddress, cfgStatsPublishAddress);
    cfgDefaults.getOptionalValue<double>(cfgDefaultsEntryPoint + ".statsPublishInterval", cfgStatsPublishInterval, cfgStatsPublishInterval);
    cfgDefaults.getOptionalValue<std::string>(cfgDefaultsEntryPoint + ".db", cfgDatabaseCxParams);
    cfgDefaults.getOptionalValue<int>(cfgDefaultsEntryPoint + ".customCommandsEnabled", cfgCustomCommandsEnabled);
    cfgDefaults.getOptionalValue<std::string>(cfgDefaultsEntryPoint + ".membanksMonitorPath", cfgMembanksMonitorPath, cfgMembanksMonitorPath);
    cfgDefaults.getOptionalValue<double>(cfgDefaultsEntryPoint + ".membanksMonitorRate", cfgMembanksMonitorRate, cfgMembanksMonitorRate);
  }
  catch(...) {
    //initLogs.push_back({LogWarningSupport_(3100), std::string("Error loading defaults")});
  }

  // redirect executable (if different from self!)
  if (readoutExe.length() && readoutExe!=argv[0]) {
    std::vector<char*> argv2;
    argv2.push_back((char *)readoutExe.c_str());
    if (readoutConfig.length()) {
      argv2.push_back((char *)readoutConfig.c_str());
    }
    for (int i=argv2.size(); i<argc; i++) {
      argv2.push_back(argv[i]);
    }
    printf("Launching ");
    for (auto const &a : argv2) {
      printf("%s ",a);
    }
    printf("\n");
    argv2.push_back(NULL);
    execv(readoutExe.c_str(), &argv2[0]);
    printf("Failed to execute : %s\n",strerror(errno));
    exit(1);
  }

  // before anything, ensure all memory used by readout is kept in RAM
  if (doMemLock) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
      initLogs.push_back({LogInfoDevel, "Memory locked"});
    } else {
      initLogs.push_back({LogWarningSupport_(3230), "Failed to lock memory"});
    }
  }

  if (argc < 2) {
    printf("Please provide path to configuration file\n");
    return -1;
  }
  cfgFileURI = argv[1];
  if (argc > 2) {
    cfgFileEntryPoint = argv[2];
  }

  // init stats
  memcpy(gReadoutStats.counters.source, occRole.c_str(),
    occRole.length() >= sizeof(gReadoutStats.counters.source) ? (sizeof(gReadoutStats.counters.source) - 1) : occRole.length());
  gReadoutStats.counters.state = stringToUint64("standby");
  int readoutStatsErr = gReadoutStats.startPublish(cfgStatsPublishAddress, cfgStatsPublishInterval);
  if (readoutStatsErr == 0) {
    initLogs.push_back({LogInfoDevel, "Started Stats publish @ " + cfgStatsPublishAddress});
  } else if (readoutStatsErr > 0) {  
    initLogs.push_back({LogWarningDevel_(3236), "Failed to start Stats publish"});
  } //otherwise: disabled
  
  if (cfgMembanksMonitorRate > 0) {
    theMemoryBankManager.startMonitoring(cfgMembanksMonitorRate, cfgMembanksMonitorPath.c_str());
    initLogs.push_back({LogInfoDevel, "Started MemoryBanks monitor @ " + std::to_string(cfgMembanksMonitorRate) + " Hz : " + theMemoryBankManager.getMonitorFifoPath(-1) + "..."});
  }

  // configure signal handlers for clean exit
  struct sigaction signalSettings;
  bzero(&signalSettings, sizeof(signalSettings));
  signalSettings.sa_handler = signalHandler;
  sigaction(SIGTERM, &signalSettings, NULL);
  sigaction(SIGQUIT, &signalSettings, NULL);
  sigaction(SIGINT, &signalSettings, NULL);
  signal(SIGPIPE, SIG_IGN);

  // log startup and options
  theLog.log(LogInfoSupport_(3001), "Readout " READOUT_VERSION " - process starting, pid %d for role %s", getpid(), occRole.c_str());
  if (cfgVerbose) {
    theLog.log(LogInfoDevel, "Build time: %s %s", __DATE__, __TIME__);
    theLog.log(LogInfoDevel, "Optional built features enabled:");
    #ifdef WITH_READOUTCARD
      theLog.log(LogInfoDevel, "READOUTCARD : yes (v%s)", o2::roc::getReadoutCardVersion());
    #else
      theLog.log(LogInfoDevel, "READOUTCARD : no");
    #endif
    #ifdef WITH_CONFIG
      theLog.log(LogInfoDevel, "CONFIG : yes");
    #else
      theLog.log(LogInfoDevel, "CONFIG : no");
    #endif
    #ifdef WITH_FAIRMQ
      theLog.log(LogInfoDevel, "FAIRMQ : yes");
      // redirect FMQ logs to infologger
      setFMQLogsToInfoLogger(&theLog);
    #else
      theLog.log(LogInfoDevel, "FAIRMQ : no");
    #endif
    #ifdef WITH_NUMA
      theLog.log(LogInfoDevel, "NUMA : yes");
    #else
      theLog.log(LogInfoDevel, "NUMA : no");
    #endif
    #ifdef WITH_RDMA
      theLog.log(LogInfoDevel, "RDMA : yes");
    #else
      theLog.log(LogInfoDevel, "RDMA : no");
    #endif
    #ifdef WITH_OCC
      theLog.log(LogInfoDevel, "OCC : yes");
    #else
      theLog.log(LogInfoDevel, "OCC : no");
    #endif
    #ifdef WITH_LOGBOOK
      theLog.log(LogInfoDevel, "LOGBOOK : yes");
    #else
      theLog.log(LogInfoDevel, "LOGBOOK : no");
    #endif
    #ifdef WITH_DB
      theLog.log(LogInfoDevel, "DB : yes");
    #else
      theLog.log(LogInfoDevel, "DB : no");
    #endif
    #ifdef WITH_ZMQ
      theLog.log(LogInfoDevel, "ZMQ : yes");
    #else
      theLog.log(LogInfoDevel, "ZMQ : no");
    #endif
    #ifdef WITH_GPERFTOOLS
      theLog.log(LogInfoDevel, "GPERFTOOLS : yes");
    #else
      theLog.log(LogInfoDevel, "GPERFTOOLS : no");
    #endif
    theLog.log(LogInfoDevel, "Working directory: %s", std::filesystem::current_path().c_str());
  }

  // report cached logs
  for(auto const &l : initLogs) {
    theLog.log(l.first, "%s", l.second.c_str());
  }

  // init database
  if (cfgDatabaseCxParams != "") {
    #ifdef WITH_DB
      try {
        dbHandle=std::make_unique<ReadoutDatabase>(cfgDatabaseCxParams.c_str(), cfgVerbose, dbLog);
	if (dbHandle == nullptr) { throw __LINE__; }
	theLog.log(LogInfoDevel_(3012), "Database connected");
      }
      catch(...) {
        theLog.log(LogWarningDevel_(3242), "Failed to connect database");
      }
    #endif
  }

  // init shell for custom commands
  if (cfgCustomCommandsEnabled) {
    for (;;) {
      int p_stdin[2], p_stdout[2], p_stderr[2];
      pid_t pid;

      if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0 || pipe(p_stderr) != 0) break;

      // create shell launching command
      std::vector<std::string> shellArgvStrings;
      getListFromString(cfgCustomCommandsShell, shellArgvStrings, ' ');
      if (shellArgvStrings.size()<1) {
        break;
      }
      theLog.log(LogInfoDevel_(3013), "Executing %s", cfgCustomCommandsShell.c_str());

      pid = fork();
      if (pid < 0) {
        break;
      } else if (pid == 0) {
        dup2(p_stdin[0], STDIN_FILENO);
        dup2(p_stdout[1], STDOUT_FILENO);
        dup2(p_stderr[1], STDERR_FILENO);
	close(p_stdin[0]);
	close(p_stdin[1]);
	close(p_stdout[0]);
	close(p_stdout[1]);
        close(p_stderr[0]);
        close(p_stderr[1]);

        std::vector<const char*> argv;
        std::string filename = std::filesystem::path(shellArgvStrings[0]).filename();
        argv.push_back(filename.c_str());
        for(unsigned int i = 1; i< shellArgvStrings.size(); i++) {
          argv.push_back(shellArgvStrings[i].c_str());
        }
        argv.push_back(NULL);
        fprintf(stderr, "%s\n", shellArgvStrings[0].c_str());
        execv(shellArgvStrings[0].c_str(), (char* const*)&argv[0]);
        fprintf(stderr, "Failed to start shell for custom commands: %s\n", strerror(errno));
        exit(1);
      }
      close(p_stdin[0]);
      close(p_stdout[1]);
      close(p_stderr[1]);
      customCommandsShellFdIn = p_stdin[1];
      customCommandsShellFdOut = p_stdout[0];
      customCommandsShellFdErr = p_stderr[0];
      customCommandsShellPid = pid;
      break;
    }
    if (customCommandsShellPid) {
      // create a thread to redirect stderr shell output
      customCommandsShellLog = std::make_unique<TheLogRedirection>(customCommandsShellFdErr, "shell", 3013);
      theLog.log(LogInfoDevel_(3013), "Process started for custom commands - pid %d", (int)customCommandsShellPid);
    } else {
      cfgCustomCommandsEnabled = 0;
    }
  }
  // print some built-in constants
  if (cfgVerbose) {
    // theLog.log(LogInfoDevel, "DataBlockHeader size = %d", (int)sizeof(DataBlockHeader));
  }

  return 0;
}

//#include <boost/property_tree/json_parser.hpp>

// dump boost ptree on stdout
void printTree (boost::property_tree::ptree &pt, int level) {
  if (pt.empty()) {
    std::cerr << "\""<< pt.data()<< "\"";
  }

  else {
    if (level) std::cerr << std::endl;
    std::cerr << std::string(level*2, ' ') << "{" << std::endl;
    for (boost::property_tree::ptree::iterator pos = pt.begin(); pos != pt.end();) {
      std::cerr << std::string((level+1)*2, ' ') << "\"" << pos->first << "\": ";
      printTree(pos->second, level + 1);
      ++pos;
      if (pos != pt.end()) {
        std::cerr << ",";
      }
      std::cerr << std::endl;
    }
   std::cerr << std::string(level*2, ' ') << " }";
  }
  return;
}

// merge content of boost ptree pt2 into pt1
// existing nodes are NOT overwritten
void mergeTree (boost::property_tree::ptree &pt1, boost::property_tree::ptree &pt2) {
  // iterate each node in pt2
  for (boost::property_tree::ptree::iterator pos = pt2.begin(); pos != pt2.end(); ++pos) {
    auto nodeName = pos->first;
    // is there a key match in pt1 ?
    auto sub1 = pt1.get_child_optional(nodeName);
    if (!sub1) {
      // no match, insert
      pt1.put_child(nodeName, pos->second);
    } else {
      // match, merge subtrees
      mergeTree(sub1.get(), pos->second);
    }
  }
}

int Readout::_configure(const boost::property_tree::ptree& properties)
{
  theLog.log(LogInfoSupport_(3005), "Readout executing CONFIGURE");
  gReadoutStats.counters.state = stringToUint64("> conf");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  // reset some flags
  gReadoutStats.isFairMQ = 0; // disable FMQ stats

  // reset monitoring queue
  gReadoutMonitoringQueue.clear();

  // load configuration file
  theLog.log(LogInfoSupport, "Reading configuration from %s %s", cfgFileURI, cfgFileEntryPoint);

  auto loadConfig = [] (const char* cfgFileURI, const char *cfgFileEntryPoint, ConfigFile &cfg) {
    try {
      // check URI prefix
      if (!strncmp(cfgFileURI, "file:", 5)) {
        // let's use the 'Common' config file library
        cfg.load(cfgFileURI);
      } else {
        // otherwise use the Configuration module, if available
        #ifdef WITH_CONFIG
          try {
            std::unique_ptr<o2::configuration::ConfigurationInterface> conf = o2::configuration::ConfigurationFactory::getConfiguration(cfgFileURI);
            boost::property_tree::ptree t = conf->getRecursive(cfgFileEntryPoint);
            cfg.load(t);
            // cfg.print();
          } catch (std::exception& e) {
            throw std::string(e.what());
          }
        #else
          throw std::string("This type of URI is not supported");
        #endif
      }
    } catch (std::string err) {
      theLog.log(LogErrorSupport_(3100), "Failed to read config: %s", err.c_str());
      return -1;
    }
    return 0;
  };

  if (loadConfig(cfgFileURI, cfgFileEntryPoint, cfg)) {
    return -1;
  }

  //printTree(cfg.get(),0);

  // configuration parameter: | readout | defaults | string |  | If set, the corresponding configuration URI is loaded and merged with current readout configuration. Existing parameters in current config are NOT overwritten. |

  // try to load extra defaults, if defined, and merge
  std::string cfgDefaultsPath = "";
  if (cfg.getOptionalValue<std::string>("readout.defaults", cfgDefaultsPath) == 0) {
    ConfigFile cfgDefaults;
    theLog.log(LogInfoDevel, "Reading configuration defaults from %s %s", cfgDefaultsPath.c_str(), cfgFileEntryPoint);
    if (loadConfig(cfgDefaultsPath.c_str(), cfgFileEntryPoint, cfgDefaults) == 0) {
      //printTree(cfgDefaults.get(),0);
      // merge trees: defaults values used if not defined already
      mergeTree(cfg.get(), cfgDefaults.get());
      //printTree(cfg.get(),0);
    } else {
     return -1;
    }
  }

  // TODO
  // save config somewhere ?

  // apply provided occ properties over loaded configuration
  // with function to overwrtie configuration tree t1 with (selected) content of t2
  auto mergeConfig = [&](boost::property_tree::ptree& t1, const boost::property_tree::ptree& t2) {
    theLog.log(LogInfoDevel, "Merging selected content of OCC configuration");
    try {
      // overwrite fairmq channel parameters
      // get list of channels
      if (t2.get_child_optional("chans")) {
        boost::property_tree::ptree& ptchannels = (boost::property_tree::ptree&)t2.get_child("chans");
        theLog.log(LogInfoDevel, "Found OCC FMQ channels configuration");
        for (auto const& pos : ptchannels) {
          std::string channelName = boost::lexical_cast<std::string>(pos.first);
          // check for a consumer with same fairmq channel
          for (auto kName : ConfigFileBrowser(&cfg, "consumer-")) {
            std::string cfgType;
            cfg.getOptionalValue<std::string>(kName + ".consumerType", cfgType);
            if (cfgType == "FairMQChannel") {
              std::string cfgChannelName;
              cfg.getOptionalValue<std::string>(kName + ".fmq-name", cfgChannelName);
              if (cfgChannelName == channelName) {
                // this is matching, let's overwrite t1 with content of t2
                theLog.log(LogInfoDevel, "Updating %s - FairMQ channel %s :", kName.c_str(), channelName.c_str());
                std::string progOptions;
                for (auto const& pos2 : pos.second.get_child("0")) {
                  std::string paramName = pos2.first.c_str();
                  std::string paramValue = pos2.second.data();
                  if ((paramName == "transport") || (paramName == "type") || (paramName == "address")) {
                    std::string cfgKey = kName + ".fmq-" + paramName;
                    theLog.log(LogInfoDevel, "%s = %s", cfgKey.c_str(), paramValue.c_str());
                    t1.put(cfgKey.c_str(), paramValue.c_str());
                  } else {
                    // add it as a program option
                    if (progOptions != "") {
                      progOptions += ",";
                    }
                    progOptions += paramName + "=" + paramValue;
                  }
                }
                // set FMQ program options, if any
                if (progOptions != "") {
                  std::string cfgKey = kName + ".fmq-progOptions";
                  theLog.log(LogInfoDevel, "%s = %s", cfgKey.c_str(), progOptions.c_str());
                  t1.put(cfgKey.c_str(), progOptions.c_str());
                }
              }
            }
          }
        }
      } else {
        theLog.log(LogInfoDevel, "No OCC FMQ channels configuration found");
      }
    } catch (std::exception& e) {
      theLog.log(LogErrorSupport_(3100), "%s", e.what());
    }
  };
  mergeConfig(cfg.get(), properties);

  // merge default trees
  const char *defaultTag = "-*";
  // if a section ends with above string, its parameters are copied to all matching section names, when these parameters are not already defined there
  // append configuration tree t1 with content of t2 (existing leaves in t1 not overwritten by same ones from t2)
  auto appendConfig = [&](boost::property_tree::ptree& t1, boost::property_tree::ptree& t2) {
    for (boost::property_tree::ptree::iterator it = t2.begin(); it != t2.end();++it) {
      if (!t1.get_child_optional(it->first)) {
        //printf("set %s = %s\n", it->first.c_str(),it->second.data().c_str());
        t1.put_child( it->first, it->second );
      }
    }
  };
  // function returns true when end of string matches tag
  auto isEndOfStringMatching = [&](const std::string &s, const char* tag) {
    if (s.length()<=strlen(tag)) return false;
    if (s.substr(s.length()-strlen(tag)) != tag) return false;
    return true;
  };
  for (boost::property_tree::ptree::iterator pos = cfg.get().begin(); pos != cfg.get().end();) {    
    const std::string section = pos->first;
    if (!isEndOfStringMatching(section, defaultTag)) {
      ++pos;
      continue;
    }
    auto smatch = section.substr(0,section.length()-strlen(defaultTag));
    for (boost::property_tree::ptree::iterator pos2 = cfg.get().begin(); pos2 != cfg.get().end();++pos2) {
      if (pos2 == pos) continue; // do not self-overwrite
      const std::string section2 = pos2->first;
      if (section2.compare(0,smatch.length(),smatch)) continue; // mismatch
      if (isEndOfStringMatching(section2, defaultTag)) continue; // do not overwrite other defaults sections
      theLog.log(LogInfoDevel_(3002), "Updating configuration section [%s] with defaults from [%s]", pos2->first.c_str(), pos->first.c_str());
      appendConfig(pos2->second,pos->second);
    }
    // delete section with defaults, to avoid it is used further
    pos = cfg.get().erase(pos);
  }

  // resolve "symlinks"
  int cfgLinksErrors = 0;
  struct ConfigCache {
    std::string URI;
    std::string EntryPoint;
    std::unique_ptr<ConfigFile> cfg;
  };
  std::vector<ConfigCache> cfgCache;
  int nSubstitutions = 0;
  std::function<void(boost::property_tree::ptree &, const std::string &)> resolveLinks;
  resolveLinks = [&resolveLinks, &cfgLinksErrors, &cfgCache, &loadConfig, &nSubstitutions, this](boost::property_tree::ptree &pt, const std::string &key) -> void {
    if (pt.empty()) {
      std::string value = pt.data();
      const std::string keywordLink = "@LINK";
      if (value.compare(0, keywordLink.length(), keywordLink) == 0) {
        // this is a symlink
	// extract reference
	// syntax: @LINK,URI,EntryPoint,Path
        std::vector<std::string> linkArgs;
        getListFromString(value, linkArgs, ',');
	if (linkArgs.size() != 4) {
          theLog.log(LogErrorSupport_(3102), "Failed to parse link: %s = %s", key.c_str(), value.c_str());
	  cfgLinksErrors++;
	  return;
	}
	const char* cfgLinkUri = linkArgs[1].c_str();
	const char* cfgLinkEntryPoint = linkArgs[2].c_str();
	const char* cfgLinkPath = linkArgs[3].c_str();

        // check if link URI starts with a relative path - and if so, compute address from current URI
        // https://en.wikipedia.org/wiki/Uniform_Resource_Identifier
        // theLog.log(LogInfoSupport, "Resolving %s / %s", cfgFileURI, cfgLinkUri);
        std::string resolvedUri;
        if (!strncmp(cfgLinkUri, "../", 3) || !strncmp(cfgLinkUri, "./", 2)) {
	  std::pair<std::string, std::string> splitPath = splitURI(cfgFileURI);
          resolvedUri = splitPath.first + std::filesystem::path((std::filesystem::path(splitPath.second).parent_path().string() + "/" + cfgLinkUri)).lexically_normal().string();
          theLog.log(LogInfoSupport, "Using relative path: %s -> %s", cfgLinkUri, resolvedUri.c_str());
          cfgLinkUri = resolvedUri.c_str();
	}

	// search for file in cache
	unsigned int ix = 0;
	for(;ix < cfgCache.size(); ix++) {
	  if ((cfgCache[ix].URI == cfgLinkUri) && (cfgCache[ix].EntryPoint == cfgLinkEntryPoint)) {
	    break;
	  }
	}
	if (ix == cfgCache.size()) {
	  // no match in cache, add it
	  try {
            auto cfg = std::make_unique<ConfigFile>();
            if (cfg==nullptr) {
	      throw __LINE__;
	    }
            if (loadConfig(cfgLinkUri, cfgLinkEntryPoint, *cfg)) {
	      throw __LINE__;
	    }
	    cfgCache.push_back({cfgLinkUri, cfgLinkEntryPoint, std::move(cfg)});
	  }
	  catch(...) {
	    theLog.log(LogErrorSupport_(3102), "Failed to load linked configuration %s %s", cfgLinkUri, cfgLinkEntryPoint);
	    cfgLinksErrors++;
	    return;
	  }
          theLog.log(LogInfoSupport, "Reading linked configuration from %s %s", cfgLinkUri, cfgLinkEntryPoint);
	}
	// at this stage we now have a valid config in cache at index ix
	// get value from linked config
	std::string linkValue;
        if (cfgCache[ix].cfg->getOptionalValue<std::string>(cfgLinkPath, linkValue)) {
	    theLog.log(LogErrorSupport_(3102), "Failed to get link value: %s = %s", key.c_str(), value.c_str());
	    cfgLinksErrors++;
	    return;
	}
        theLog.log(LogInfoDevel_(3002), "Link substituted : %s = %s -> %s", key.c_str(), value.c_str(), linkValue.c_str());
	pt.data() = linkValue;
	nSubstitutions++;
      }
      return;
    }
    for (boost::property_tree::ptree::iterator pos = pt.begin(); pos != pt.end();++pos) {
      //printf("%s\n", pos->first.c_str());
      resolveLinks(pos->second, pos->first.c_str());
    }
  };
  int maxLoops = 5;
  for (int i = 0; i <= maxLoops; i++) {
    if (i == maxLoops) {
      theLog.log(LogErrorSupport_(3100), "Links not fully resolved after %d iterations, there might be some circular dependencies in the configuration", maxLoops);
      cfgLinksErrors++;
      break;
    }
    nSubstitutions = 0;
    resolveLinks(cfg.get(),"");
    if (nSubstitutions == 0) {
      break;
    }
  }
  if (cfgLinksErrors) {
    theLog.log(LogErrorSupport_(3100), "Some links in the configuration could not be resolved");
    return -1;
  }


  // extract optional configuration parameters
  // configuration parameter: | readout | customCommands | string | | List of key=value pairs defining some custom shell commands to be executed at before/after state change commands. |
  if (customCommandsShellPid) {
    std::string cfgCustomCommandsList;
    customCommands.clear();
    cfg.getOptionalValue<std::string>("readout.customCommands", cfgCustomCommandsList);
    if (getKeyValuePairsFromString(cfgCustomCommandsList, customCommands)) {
      theLog.log(LogWarningDevel_(3102), "Failed to parse custom commands");
      customCommands.clear();
    } else {
      if (customCommands.size()) {
        theLog.log(LogInfoDevel_(3013), "Registered custom commands:");
      }
      for (const auto &kv : customCommands) {
	theLog.log(LogInfoDevel_(3013), "%s : %s", kv.first.c_str(), kv.second.c_str());
      }
    }
  }

  // execute custom command
  executeCustomCommand("preCONFIGURE");

  // try to prevent deep sleeps
  bool deepsleepDisabled = false;
  int maxLatency = 2;
  latencyFd = open("/dev/cpu_dma_latency", O_WRONLY);
  if (latencyFd < 0) {
    // theLog.log(LogDebugDevel, "Can not open /dev/cpu_dma_latency");
  } else {
    if (write(latencyFd, &maxLatency, sizeof(maxLatency)) != sizeof(maxLatency)) {
      // theLog.log(LogDebugDevel, "Can not write to /dev/cpu_dma_latency");
    } else {
      deepsleepDisabled = true;
    }
  }
  if (deepsleepDisabled) {
    theLog.log(LogInfoDevel, "CPU deep sleep disabled for process");
  } else {
    theLog.log(LogInfoDevel, "CPU deep sleep not disabled for process");
  }

  // configuration parameter: | readout | exitTimeout | double | -1 | Time in seconds after which the program exits automatically. -1 for unlimited. |
  cfgExitTimeout = -1;
  cfg.getOptionalValue<double>("readout.exitTimeout", cfgExitTimeout);
  if (standaloneMode) {

    auto scanTime = [&](const std::string paramName, int& t) {
      std::string s;
      cfg.getOptionalValue<std::string>(paramName, s);

      if (s.length()) {
        bool isOk = 0;
        // scan full date/time
        // format: 2021-01-31 23:30:00 or just 23:30:00 (local time)
        int year, month, day, hour, minute, second;
        time_t now = time(nullptr);
        struct tm* ts = localtime(&now);
        if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
          ts->tm_year = year - 1900;
          ts->tm_mon = month - 1;
          ts->tm_mday = day;
          ts->tm_hour = hour;
          ts->tm_min = minute;
          ts->tm_sec = second;
          t = mktime(ts);
          isOk = 1;
        } else if (sscanf(s.c_str(), "%d:%d:%d", &hour, &minute, &second) == 3) {
          ts->tm_hour = hour;
          ts->tm_min = minute;
          ts->tm_sec = second;
          t = mktime(ts);
          isOk = 1;
        }
        if (!isOk) {
          theLog.log(LogErrorSupport_(3102), "Wrong value for parameter %s = %s", paramName.c_str(), s.c_str());
        }
      }
    };

    // configuration parameter: | readout | timeStart | string | | In standalone mode, time at which to execute start. If not set, immediately. |
    scanTime("readout.timeStart", cfgTimeStart);

    // configuration parameter: | readout | timeStop | string | | In standalone mode, time at which to execute stop. If not set, on int/term/quit signal. |
    scanTime("readout.timeStop", cfgTimeStop);

    // configuration parameter: | readout | numberOfRuns | int | 1 | In standalone mode, number of runs to execute (ie START/STOP cycles). |
    cfg.getOptionalValue<int>("readout.numberOfRuns", cfgNumberOfRuns);
  }

  cfgMaxMsgError = 0;
  cfgMaxMsgWarning = 0;
  // configuration parameter: | readout | maxMsgError | int | 0 | If non-zero, maximum number of error messages allowed while running. Readout stops when threshold is reached. |
  cfg.getOptionalValue<int>("readout.maxMsgError", cfgMaxMsgError);
  // configuration parameter: | readout | maxMsgWarning | int | 0 | If non-zero, maximum number of error messages allowed while running. Readout stops when threshold is reached. |
  cfg.getOptionalValue<int>("readout.maxMsgWarning", cfgMaxMsgWarning);

  // configuration parameter: | readout | flushEquipmentTimeout | double | 1 | Time in seconds to wait for data once the equipments are stopped. 0 means stop immediately. |
  cfgFlushEquipmentTimeout = 1;
  cfg.getOptionalValue<double>("readout.flushEquipmentTimeout", cfgFlushEquipmentTimeout);
  // configuration parameter: | readout | flushConsumerTimeout | double | 1 | Time in seconds to wait before stopping the consumers (ie wait allocated pages released). 0 means stop immediately. |
  cfgFlushConsumerTimeout = 1;
  cfg.getOptionalValue<double>("readout.flushConsumerTimeout", cfgFlushConsumerTimeout);
  // configuration parameter: | readout | memoryPoolStatsEnabled | int | 0 | Global debugging flag to enable statistics on memory pool usage (printed to stdout when pool released). |
  int cfgMemoryPoolStatsEnabled = 0;
  cfg.getOptionalValue<int>("readout.memoryPoolStatsEnabled", cfgMemoryPoolStatsEnabled);
  extern int MemoryPagesPoolStatsEnabled;
  MemoryPagesPoolStatsEnabled = cfgMemoryPoolStatsEnabled;
  // configuration parameter: | readout | disableAggregatorSlicing | int | 0 | When set, the aggregator slicing is disabled, data pages are passed through without grouping/slicing. |
  cfgDisableAggregatorSlicing = 0;
  cfg.getOptionalValue<int>("readout.disableAggregatorSlicing", cfgDisableAggregatorSlicing);
  // configuration parameter: | readout | aggregatorSliceTimeout | double | 0 | When set, slices (groups) of pages are flushed if not updated after given timeout (otherwise closed only on beginning of next TF, or on stop). |
  cfgAggregatorSliceTimeout = 0;
  cfg.getOptionalValue<double>("readout.aggregatorSliceTimeout", cfgAggregatorSliceTimeout);
  // configuration parameter: | readout | aggregatorStfTimeout | double | 0 | When set, subtimeframes are buffered until timeout (otherwise, sent immediately and independently for each data source). |
  cfgAggregatorStfTimeout = 0;
  cfg.getOptionalValue<double>("readout.aggregatorStfTimeout", cfgAggregatorStfTimeout);
  // configuration parameter: | readout | tfRateLimit | double | 0 | When set, the output is limited to a given timeframe rate. |
  cfgTfRateLimit = 0;
  cfg.getOptionalValue<double>("readout.tfRateLimit", cfgTfRateLimit);
  // configuration parameter: | readout | tfRateLimitMode | int | 0 | Defines the source for TF rate limit: 0 = use TF id, 1 = use number of TF. |
  cfgTfRateLimitMode = 0;
  cfg.getOptionalValue<int>("readout.tfRateLimitMode", cfgTfRateLimitMode);

  // configuration parameter: | readout | disableTimeframes | int | 0 | When set, all timeframe related features are disabled (this may supersede other config parameters). |
  cfgDisableTimeframes = 0;
  cfg.getOptionalValue<int>("readout.disableTimeframes", cfgDisableTimeframes);
  if (cfgDisableTimeframes) {
    cfgDisableAggregatorSlicing = 1;
    cfgTfRateLimit = 0;
    theLog.log(LogInfoDevel, "Timeframes disabled");
  }

  if (cfgTfRateLimit > 0) {
    theLog.log(LogInfoDevel, "Timeframe rate limit = % .2lf Hz", cfgTfRateLimit);
  }


  // configuration parameter: | readout | logbookEnabled | int | 0 | When set, the logbook is enabled and populated with readout stats at runtime. |
  cfgLogbookEnabled = 0;
  cfg.getOptionalValue<int>("readout.logbookEnabled", cfgLogbookEnabled);

  // configuration parameter: | readout | logbookUpdateInterval | int | 30 | Amount of time (in seconds) between logbook publish updates. |
  cfgLogbookUpdateInterval = 30;
  cfg.getOptionalValue<int>("readout.logbookUpdateInterval", cfgLogbookUpdateInterval);

  if (cfgLogbookEnabled) {
#ifndef WITH_LOGBOOK
    theLog.log(LogErrorDevel_(3210), "Bookkeeping enabled in configuration, but feature not available in this build");
#else
    // configuration parameter: | readout | logbookUrl | string | | The address to be used for the logbook API. |
    cfg.getOptionalValue<std::string>("readout.logbookUrl", cfgLogbookUrl);

    theLog.log(LogInfoDevel, "Logbook enabled, %ds update interval, using URL = %s", cfgLogbookUpdateInterval, cfgLogbookUrl.c_str());
    auto logbookHandle = o2::bkp::api::BkpClientFactory::create(cfgLogbookUrl);
    if (logbookHandle == nullptr) {
      theLog.log(LogErrorSupport_(3210), "Failed to create handle to bookkeeping");
    } else {
      theLogbookThread = std::make_unique<LogbookThread>(std::move(logbookHandle));
    }
#endif
  }

  // configuration parameter: | readout | timeframeServerUrl | string | | The address to be used to publish current timeframe, e.g. to be used as reference clock for other readout instances. |
  cfg.getOptionalValue<std::string>("readout.timeframeServerUrl", cfgTimeframeServerUrl);
  if (cfgTimeframeServerUrl.length() > 0) {
#ifdef WITH_ZMQ
    theLog.log(LogInfoDevel, "Creating Timeframe server @ %s", cfgTimeframeServerUrl.c_str());
    tfServer = std::make_unique<ZmqServer>(cfgTimeframeServerUrl);
    if (tfServer == nullptr) {
      theLog.log(LogErrorDevel_(3220), "Failed to create TF server");
    }
#else
    theLog.log(LogWarningSupport_(3101), "Skipping timeframeServer - not supported by this build");
#endif
  }

  // configuration parameter: | readout | externalSyncServer | string | | If set, ZMQ address to request SYNC signal at SOR. |
  cfg.getOptionalValue<std::string>("readout.externalSyncServer", cfgExternalSyncServer);
  // configuration parameter: | readout | externalSyncTimeout | int | 3000 | Timeout (in milliseconds) to wait for the SYNC signal at SOR (when externalSyncServer is defined). |
  cfg.getOptionalValue<int>("readout.externalSyncTimeout", cfgExternalSyncTimeout, 3000);
  if (cfgExternalSyncServer != "") {
#ifdef WITH_ZMQ
    theLog.log(LogInfoDevel_(3002), "External SOR synchronization enabled: server = %s timeout = %dms", cfgExternalSyncServer.c_str(), cfgExternalSyncTimeout);
#else
    theLog.log(LogWarningSupport_(3101), "Skipping SOR synchronization - not supported by this build");
#endif
  }

  #ifdef WITH_FAIRMQ
  // configuration parameter: | readout | fairmqConsoleSeverity | int | -1 | Select amount of FMQ messages with fair::Logger::SetConsoleSeverity(). Value as defined in Severity enum defined from FairLogger/Logger.h. Use -1 to leave current setting. |
  int cfgFairmqConsoleSeverity = -1;
  cfg.getOptionalValue<int>("readout.fairmqConsoleSeverity", cfgFairmqConsoleSeverity);
  if (cfgFairmqConsoleSeverity >= 0) {
     unsetFMQLogsToInfoLogger(); // default redirection was set already on readout startup, so unset it first.
     fair::Logger::SetConsoleSeverity((fair::Severity)cfgFairmqConsoleSeverity); // set console severity
     setFMQLogsToInfoLogger(&theLog); // redirect
   }
  #endif

  // configuration of memory banks
  int numaNodeChanged = 0;
  for (auto kName : ConfigFileBrowser(&cfg, "bank-")) {
    // skip disabled
    int enabled = 1;
    // configuration parameter: | bank-* | enabled | int | 1 | Enable (1) or disable (0) the memory bank. |
    cfg.getOptionalValue<int>(kName + ".enabled", enabled);
    if (!enabled) {
      continue;
    }

    // bank size
    // configuration parameter: | bank-* | size | bytes | | Size of the memory bank, in bytes. |
    std::string cfgSize = "";
    cfg.getOptionalValue<std::string>(kName + ".size", cfgSize);
    long long mSize = ReadoutUtils::getNumberOfBytesFromString(cfgSize.c_str());
    if (mSize <= 0) {
      theLog.log(LogErrorSupport_(3100), "Skipping memory bank %s:  wrong size %s", kName.c_str(), cfgSize.c_str());
      continue;
    }

    // bank type
    // configuration parameter: | bank-* | type | string| | Support used to allocate memory. Possible values: malloc, MemoryMappedFile. |
    std::string cfgType = "";
    cfg.getOptionalValue<std::string>(kName + ".type", cfgType);
    if (cfgType.length() == 0) {
      theLog.log(LogErrorSupport_(3100), "Skipping memory bank %s:  no type specified", kName.c_str());
      continue;
    }

    // numa node
    // configuration parameter: | bank-* | numaNode | int | -1| Numa node where memory should be allocated. -1 means unspecified (system will choose). |
    int cfgNumaNode = -1;
    cfg.getOptionalValue<int>(kName + ".numaNode", cfgNumaNode);

    // instanciate new memory pool
    if (cfgNumaNode >= 0) {
#ifdef WITH_NUMA
      struct bitmask* nodemask;
      nodemask = numa_allocate_nodemask();
      if (nodemask == NULL) {
        return -1;
      }
      numa_bitmask_clearall(nodemask);
      numa_bitmask_setbit(nodemask, cfgNumaNode);
      numa_set_membind(nodemask);
      numa_free_nodemask(nodemask);
      theLog.log(LogInfoDevel, "Enforcing memory allocated on NUMA node %d", cfgNumaNode);
      numaNodeChanged = 1;
#endif
    }
    theLog.log(LogInfoDevel, "Creating memory bank %s: type %s size %lld", kName.c_str(), cfgType.c_str(), mSize);
    std::shared_ptr<MemoryBank> b = nullptr;
    try {
      b = getMemoryBank(mSize, cfgType, kName);
    } catch (...) {
    }
    if (b == nullptr) {
      theLog.log(LogErrorSupport_(3230), "Failed to create memory bank %s", kName.c_str());
      continue;
    }
    // cleanup the memory range
    b->clear();
    // add bank to list centrally managed
    theMemoryBankManager.addBank(b, kName);
    theLog.log(LogInfoDevel, "Bank %s added", kName.c_str());
  }

  // releasing memory bind policy
  if (numaNodeChanged) {
#ifdef WITH_NUMA
    struct bitmask* nodemask;
    nodemask = numa_get_mems_allowed();
    numa_set_membind(nodemask);
    // is this needed? not specified in doc...
    // numa_free_nodemask(nodemask);
    theLog.log(LogInfoDevel, "Releasing memory NUMA node enforcment");
#endif
  }

  // configuration of data consumers
  int nConsumerFailures = 0;
  for (auto kName : ConfigFileBrowser(&cfg, "consumer-")) {

    // skip disabled
    int enabled = 1;
    // configuration parameter: | consumer-* | enabled | int | 1 | Enable (value=1) or disable (value=0) the consumer. |
    cfg.getOptionalValue<int>(kName + ".enabled", enabled);
    if (!enabled) {
      continue;
    }

    // configuration parameter: | consumer-* | consumerOutput | string |  | Name of the consumer where the output of this consumer (if any) should be pushed. |
    std::string cfgOutput = "";
    cfg.getOptionalValue<std::string>(kName + ".consumerOutput", cfgOutput);

    // configuration parameter: | consumer-* | stopOnError | int | 0 | If 1, readout will stop automatically on consumer error. |
    int cfgStopOnError = 0;
    cfg.getOptionalValue<int>(kName + ".stopOnError", cfgStopOnError);

    // instanciate consumer of appropriate type
    std::unique_ptr<Consumer> newConsumer = nullptr;
    int cfgNumaNode = -1;
    (void)cfgNumaNode;
    try {
      // configuration parameter: | consumer-* | consumerType | string |  | The type of consumer to be instanciated. One of:stats, FairMQDevice, DataSampling, FairMQChannel, fileRecorder, checker, processor, tcp. |
      std::string cfgType = "";
      cfg.getOptionalValue<std::string>(kName + ".consumerType", cfgType);
      if (cfgType.length() == 0) {
        theLog.log(LogErrorSupport_(3100), "Skipping consumer %s:  no type specified", kName.c_str());
        continue;
      }
      theLog.log(LogInfoDevel, "Configuring consumer %s: %s", kName.c_str(), cfgType.c_str());

      #ifdef WITH_NUMA
	// configuration parameter: | consumer-* | numaNode | int | -1 | If set (>=0), memory / thread will try to use given NUMA node. |
	cfg.getOptionalValue<int>(kName + ".numaNode", cfgNumaNode);
	if (cfgNumaNode >= 0) {
	  theLog.log(LogInfoDevel_(3008), "Preferred NUMA node = %d", cfgNumaNode);
	  numaBind(cfgNumaNode);
	}
      #endif

      if (!cfgType.compare("stats")) {
        newConsumer = getUniqueConsumerStats(cfg, kName);
      } else if (!cfgType.compare("FairMQDevice")) {
#ifdef WITH_FAIRMQ
        newConsumer = getUniqueConsumerFMQ(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgType.c_str());
#endif
      } else if (!cfgType.compare("DataSampling")) {
#ifdef WITH_FAIRMQ
        newConsumer = getUniqueConsumerDataSampling(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgType.c_str());
#endif
      } else if (!cfgType.compare("FairMQChannel")) {
#ifdef WITH_FAIRMQ
        newConsumer = getUniqueConsumerFMQchannel(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgType.c_str());
#endif
      } else if (!cfgType.compare("fileRecorder")) {
        newConsumer = getUniqueConsumerFileRecorder(cfg, kName);
      } else if (!cfgType.compare("checker")) {
        newConsumer = getUniqueConsumerDataChecker(cfg, kName);
      } else if (!cfgType.compare("processor")) {
        newConsumer = getUniqueConsumerDataProcessor(cfg, kName);
      } else if (!cfgType.compare("tcp")) {
        newConsumer = getUniqueConsumerTCP(cfg, kName);
      } else if (!cfgType.compare("rdma")) {
#ifdef WITH_RDMA
        newConsumer = getUniqueConsumerRDMA(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgType.c_str());
#endif
      } else if (!cfgType.compare("zmq")) {
#ifdef WITH_ZMQ
        newConsumer = getUniqueConsumerZMQ(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgType.c_str());
#endif
      } else {
        theLog.log(LogErrorSupport_(3102), "Unknown consumer type '%s' for [%s]", cfgType.c_str(), kName.c_str());
      }
    } catch (const std::exception& ex) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure consumer %s : %s", kName.c_str(), ex.what());
    } catch (const std::string& ex) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure consumer %s : %s", kName.c_str(), ex.c_str());
    } catch (const char* ex) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure consumer %s : %s", kName.c_str(), ex);
    } catch (...) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure consumer %s", kName.c_str());
    }

    #ifdef WITH_NUMA
      // reset settings after instanciating consumer
      if (cfgNumaNode >= 0) {
	numaBind(-1);
      }
    #endif

    if (newConsumer != nullptr) {
      if (cfgOutput.length() > 0) {
        consumersOutput.insert(std::pair<Consumer*, std::string>(newConsumer.get(), cfgOutput));
      }
      if (cfgStopOnError) {
        newConsumer->stopOnError = 1;
      }
      dataConsumers.push_back(std::move(newConsumer));
    } else {
      nConsumerFailures++;
    }
  }

  // try to link consumers with output
  for (auto const& p : consumersOutput) {
    // search for consumer with this name
    bool found = false;
    std::string err = "not found";
    for (auto const& c : dataConsumers) {
      if (c->name == p.second) {
        if (c->isForwardConsumer) {
          err = "already used";
          break;
        }
        theLog.log(LogInfoDevel, "Output of %s will be pushed to %s", p.first->name.c_str(), c->name.c_str());
        found = true;
        c->isForwardConsumer = true;
        p.first->forwardConsumer = c.get();
        break;
      }
    }
    if (!found) {
      theLog.log(LogErrorSupport_(3100), "Failed to attach consumer %s to %s (%s)", p.first->name.c_str(), p.second.c_str(), err.c_str());
      nConsumerFailures++;
    }
  }

  if (nConsumerFailures) {
    theLog.log(LogErrorSupport_(3100), "Some consumers failed to initialize");
    return -1;
  }

  // configure readout equipments
  int nEquipmentFailures = 0; // number of failed equipment instanciation
  for (auto kName : ConfigFileBrowser(&cfg, "equipment-")) {

    // example iteration on each sub-key
    // for (auto kk : ConfigFileBrowser (&cfg,"",kName)) {
    //  printf("%s -> %s\n",kName.c_str(),kk.c_str());
    //}

    // skip disabled equipments
    // configuration parameter: | equipment-* | enabled | int | 1 | Enable (value=1) or disable (value=0) the equipment. |
    int enabled = 1;
    cfg.getOptionalValue<int>(kName + ".enabled", enabled);
    if (!enabled) {
      continue;
    }

    // configuration parameter: | equipment-* | equipmentType | string |  | The type of equipment to be instanciated. One of: dummy, rorc, cruEmulator |
    std::string cfgEquipmentType = "";
    cfg.getOptionalValue<std::string>(kName + ".equipmentType", cfgEquipmentType);
    if (cfgEquipmentType.length() == 0) {
      theLog.log(LogErrorSupport_(3100), "Skipping equipment %s:  no type specified", kName.c_str());
      continue;
    }

    theLog.log(LogInfoDevel, "Configuring equipment %s: %s", kName.c_str(), cfgEquipmentType.c_str());

    #ifdef WITH_NUMA
      // configuration parameter: | equipment-* | numaNode | string | auto | If set, memory / thread will try to use given NUMA node. If "auto", will try to guess it for given equipment (eg ROC). |
      std::string cfgNumaNode = "auto";
      cfg.getOptionalValue<std::string>(kName + ".numaNode", cfgNumaNode);
      int numaNode = -1;
      if (cfgNumaNode == "auto") {
	// equipment-specific method to get preferred NUMA node can not be implemented in a derived class method
	// because we need info here in the equipment base class before allocating the memory
	// call corresponding external function
	#ifdef WITH_READOUTCARD
          extern int getPreferredROCNumaNode(ConfigFile&, std::string);
	  if (!cfgEquipmentType.compare("rorc")) {
	    numaNode = getPreferredROCNumaNode(cfg, kName);
	  }
        #endif
      } else {
	// try to convert value to int
	int n;
	auto [ptr, ec] = std::from_chars(cfgNumaNode.data(), cfgNumaNode.data() + cfgNumaNode.size(), n);
	if (ec == std::errc{}) {
	  numaNode = n;
	}
      }
      if (numaNode >= 0) {
	theLog.log(LogInfoDevel_(3008), "Preferred NUMA node = %d", numaNode);
	numaBind(numaNode);
      }
    #endif

    std::unique_ptr<ReadoutEquipment> newDevice = nullptr;
    try {
      if (!cfgEquipmentType.compare("dummy")) {
        newDevice = getReadoutEquipmentDummy(cfg, kName);
      } else if (!cfgEquipmentType.compare("rorc")) {
#ifdef WITH_READOUTCARD
        newDevice = getReadoutEquipmentRORC(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgEquipmentType.c_str());
#endif
      } else if (!cfgEquipmentType.compare("cruEmulator")) {
        newDevice = getReadoutEquipmentCruEmulator(cfg, kName);
      } else if (!cfgEquipmentType.compare("player")) {
        newDevice = getReadoutEquipmentPlayer(cfg, kName);
      } else if (!cfgEquipmentType.compare("zmq")) {
#ifdef WITH_ZMQ
        newDevice = getReadoutEquipmentZmq(cfg, kName);
#else
        theLog.log(LogWarningSupport_(3101), "Skipping %s: %s - not supported by this build", kName.c_str(), cfgEquipmentType.c_str());
#endif
      } else {
        theLog.log(LogErrorSupport_(3102), "Unknown equipment type '%s' for [%s]", cfgEquipmentType.c_str(), kName.c_str());
      }
    } catch (std::string errMsg) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure equipment %s : %s", kName.c_str(), errMsg.c_str());
      nEquipmentFailures++;
      continue;
    } catch (int errNo) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure equipment %s : error #%d", kName.c_str(), errNo);
      nEquipmentFailures++;
      continue;
    } catch (...) {
      theLog.log(LogErrorSupport_(3100), "Failed to configure equipment %s", kName.c_str());
      nEquipmentFailures++;
      continue;
    }

    #ifdef WITH_NUMA
      // reset settings after instanciating equipment
      if (numaNode >= 0) {
	numaBind(-1);
      }
    #endif

    // add to list of equipments
    if (newDevice != nullptr) {
      readoutDevices.push_back(std::move(newDevice));
    }
  }

  if (nEquipmentFailures) {
    theLog.log(LogErrorSupport_(3100), "Some equipments failed to initialize");
    return -1;
  }

  // aggregator
  theLog.log(LogInfoDevel, "Creating aggregator");
  agg_output = std::make_unique<AliceO2::Common::Fifo<DataSetReference>>(10000);
  int nEquipmentsAggregated = 0;
  agg = std::make_unique<DataBlockAggregator>(agg_output.get(), "Aggregator");

  for (auto&& readoutDevice : readoutDevices) {
    // theLog.log(LogInfoDevel, "Adding equipment: %s",readoutDevice->getName().c_str());
    agg->addInput(readoutDevice->dataOut);
    nEquipmentsAggregated++;
  }
  theLog.log(LogInfoDevel, "Aggregator: %d equipments", nEquipmentsAggregated);

  // execute custom command
  executeCustomCommand("postCONFIGURE");

  theLog.log(LogInfoSupport_(3005), "Readout completed CONFIGURE");
  gReadoutStats.counters.state = stringToUint64("ready");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  return 0;
}

int Readout::_start()
{
  // set run number for logs
  theLogContext.setField(InfoLoggerContext::FieldName::Run, std::to_string(occRunNumber)); // this works also for undefinedRunNumber, 0 -> empty field in log API
  theLog.setContext(theLogContext);
  if (occRunNumber != undefinedRunNumber) {
    setenv(envRunNumber, std::to_string(occRunNumber).c_str(), 1);
    theLog.log(LogInfoDevel, "Run number %d", (int)occRunNumber);
  } else {
    unsetenv(envRunNumber);
    theLog.log(LogInfoDevel, "Run number not defined");
  }

  if (cfgExternalSyncServer != "") {
#ifdef WITH_ZMQ
    // sync barrier here
    // send a request to remote server and wait for SYNC reply (or timeout) before proceeding
    theLog.log(LogInfoDevel, "Readout sync starting");
    std::string msg = std::to_string((int)occRunNumber); // send run number to sync server
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_REQ);
    zmq_connect(socket, cfgExternalSyncServer.c_str());
    int timeout = cfgExternalSyncTimeout; // ms
    zmq_setsockopt(socket, ZMQ_SNDTIMEO, &timeout, sizeof(timeout)); // for zmq_send
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)); // for zmq_recv
    zmq_send(socket, msg.c_str(), msg.size(), 0);
    char buffer[256];
    int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (size == -1) {
      theLog.log(LogInfoDevel, "Readout sync timeout");
    } else {
      buffer[size] = '\0'; // for later use. no use of the reply so far.
      theLog.log(LogInfoDevel, "Readout sync done");
    }
    zmq_close(socket);
    zmq_ctx_term(context);
#endif
  }

  theLog.resetMessageCount();
  theLog.log(LogInfoSupport_(3005), "Readout executing START");
  gReadoutStats.reset(1);
  gReadoutStats.counters.state = stringToUint64("> start");
  gReadoutStats.counters.runNumber = occRunNumber;
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  // publish initial logbook statistics
  #ifdef WITH_DB
  if (dbHandle != nullptr) {
    dbHandle->initRunCounters(occRole.c_str(), occRunNumber);
  }
  #endif
  publishLogbookStats();
  logbookTimer.reset(cfgLogbookUpdateInterval * 1000000);
  currentTimeframeId = undefinedTimeframeId;
  countTimeframeId = 0;

  // execute custom command
  executeCustomCommand("preSTART");

  // cleanup exit conditions
  ShutdownRequest = 0;

  theLog.log(LogInfoDevel, "Starting aggregator");
  if (cfgDisableAggregatorSlicing) {
    theLog.log(LogInfoDevel, "Aggregator slicing disabled");
    agg->disableSlicing = 1;
  } else {
    if (cfgAggregatorSliceTimeout > 0) {
      theLog.log(LogInfoDevel, "Aggregator slice timeout = %.2lf seconds", cfgAggregatorSliceTimeout);
      agg->cfgSliceTimeout = cfgAggregatorSliceTimeout;
    }
    if (cfgAggregatorStfTimeout > 0) {
      theLog.log(LogInfoDevel, "Aggregator subtimeframe timeout = %.2lf seconds", cfgAggregatorStfTimeout);
      agg->cfgStfTimeout = cfgAggregatorStfTimeout;
      agg->enableStfBuilding = 1;
    }
  }

  agg->start();

  // notify consumers of imminent data flow start
  for (auto& c : dataConsumers) {
    c->start();
  }

  theLog.log(LogInfoDevel, "Starting readout equipments");
  for (auto&& readoutDevice : readoutDevices) {
    readoutDevice->start();
  }

  for (auto&& readoutDevice : readoutDevices) {
    readoutDevice->setDataOn();
  }

  // reset exit timeout, if any
  if (cfgExitTimeout > 0) {
    startTimer.reset(cfgExitTimeout * 1000000);
    theLog.log(LogInfoDevel, "Automatic exit in %.2f seconds", cfgExitTimeout);
  } else {
    startTimer.reset();
  }

  theLog.log(LogInfoDevel, "Running");
  isRunning = 1;

  // start thread for main loop
  std::function<void(void)> l = std::bind(&Readout::loopRunning, this);
  runningThread = std::make_unique<std::thread>(l);

  // execute custom command
  executeCustomCommand("postSTART");

  theLog.log(LogInfoSupport_(3005), "Readout completed START");
  gReadoutStats.counters.state = stringToUint64("running");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  return 0;
}

std::thread::id mainThreadId;
int isMainThread(void *) {
  if (std::this_thread::get_id() == mainThreadId) {
    return 1;
  }
  return 0;
}

void Readout::loopRunning()
{
  setThreadName("readout-loop");

  theLog.log(LogInfoDevel, "Entering main loop");
#ifdef CALLGRIND
  theLog.log(LogInfoDevel, "Starting callgrind instrumentation");
  CALLGRIND_START_INSTRUMENTATION;
#endif

#ifdef WITH_GPERFTOOLS
  theLog.log(LogInfoDevel, "Starting gperftools instrumentation");

  mainThreadId = std::this_thread::get_id();
  struct ProfilerOptions gperfopt = { &isMainThread, 0 };
  ProfilerStartWithOptions(gperfOutputFile.c_str(), &gperfopt);
  //ProfilerStart(gperfOutputFile.c_str());
#endif

  try {
  for (;;) {
    if ((!isRunning) && ((cfgFlushEquipmentTimeout <= 0) || (stopTimer.isTimeout()))) {
      break;
    }

    DataSetReference bc = nullptr;
    // check first element from incoming fifo
    if (agg_output->front(bc) == 0) {

      if (bc != nullptr) {
        // count number of subtimeframes
        if (bc->size() > 0) {
          if (bc->at(0)->getData() != nullptr) {
            uint64_t newTimeframeId = bc->at(0)->getData()->header.timeframeId;
            if (newTimeframeId != currentTimeframeId) {
              // beginning of new TF detected: are we complying with maximum TF rate ?
              if (cfgTfRateLimit > 0) {
                uint64_t maxTimeframes = floor(startTimer.getTime() * cfgTfRateLimit) + 1; // number of TF allowed at this point
                // mode 0: compare with TFid
                // mode 1: use number of TF instead of computed TF id. Useful when replaying files with jumps in TF ids.
                if (    ((cfgTfRateLimitMode == 0) && (newTimeframeId > maxTimeframes))
                     || ((cfgTfRateLimitMode == 1) && (countTimeframeId >= maxTimeframes)) ) {
                  usleep(1000);
                  continue;
                }
              }
              countTimeframeId++;
              currentTimeframeId = newTimeframeId;
#ifdef WITH_ZMQ
              if (tfServer) {
                tfServer->publish(&currentTimeframeId, sizeof(currentTimeframeId));
              }
#endif
              gReadoutStats.counters.numberOfSubtimeframes++;
	      gReadoutStats.counters.currentOrbit =  bc->at(0)->getData()->header.timeframeOrbitFirst;
	      gReadoutStats.counters.notify++;
            }
            // printf("Pushing TF #%d = %d\n", (int)countTimeframeId, (int)newTimeframeId);
          }
          for(auto const& b : *bc) {
            updatePageStateFromDataBlockContainerReference(b, MemoryPage::PageState::InConsumer);
          }
        }

        for (auto& c : dataConsumers) {
          // push only to "prime" consumers, not to those getting data directly forwarded from another consumer
          if (c->isForwardConsumer == false) {
            if (c->pushData(bc) < 0) {
              c->isError++;
            }
          }
          if ((c->isError) && (c->stopOnError)) {
            if (!c->isErrorReported) {
              theLog.log(LogErrorSupport_(3231), "Error detected in consumer %s", c->name.c_str());
              c->isErrorReported = true;
            }
            isError = 1;
          }
        }
      }

      // actually remove element from incoming fifo
      agg_output->pop(bc);

    } else {
      // we are idle...
      // todo: set configurable idling time
      usleep(1000);
    }
  }
  }
  catch (const std::exception& e) {
    theLog.log(LogErrorSupport_(3245), "Exception : %s", e.what());
  }

#ifdef CALLGRIND
  CALLGRIND_STOP_INSTRUMENTATION;
  CALLGRIND_DUMP_STATS;
  theLog.log(LogInfoDevel, "Stopping callgrind instrumentation");
#endif
#ifdef WITH_GPERFTOOLS
  theLog.log(LogInfoDevel, "Stopping gperftools instrumentation");
  ProfilerStop();
#endif

  theLog.log(LogInfoDevel, "Exiting main loop");
}

int Readout::_iterateCheck()
{
  usleep(100000);
  for (auto&& readoutDevice : readoutDevices) {
    if ((readoutDevice->isError) && (readoutDevice->stopOnError)) {
      isError = 1;
    }
    if (readoutDevice->isFatalError) {
      isError = 1;
    }
  }
  if (isError) {
    if (!logFirstError) {
      theLog.log(LogErrorSupport_(3231), "Error reported to state machine");
      logFirstError = 1;
    }
    return -1;
  }
  if ((cfgMaxMsgError > 0) && (theLog.getMessageCount(InfoLogger::Severity::Error) >= (unsigned long) cfgMaxMsgError)) {
    theLog.log(LogErrorSupport_(3231), "Maximum number of Error messages reached, stopping");
    isError = 1;
  } else if ((cfgMaxMsgWarning > 0) && (theLog.getMessageCount(InfoLogger::Severity::Warning) >= (unsigned long) cfgMaxMsgWarning)) {
    theLog.log(LogErrorSupport_(3231), "Maximum number of Warning messages reached, stopping");
    isError = 1;
  }
  return 0;
}

int Readout::_iterateRunning()
{
  usleep(100000);
  // printf("running time: %.2f\n",startTimer.getTime());
  if (ShutdownRequest) {
    theLog.log(LogInfoDevel, "Exit requested");
    return 1;
  }
  if ((cfgExitTimeout > 0) && (startTimer.isTimeout())) {
    theLog.log(LogInfoDevel, "Exit timeout reached, %.2fs elapsed", cfgExitTimeout);
    return 1;
  }
  if (isError) {
    if (!logFirstError) {
      theLog.log(LogErrorSupport_(3231), "Error reported to state machine");
      logFirstError = 1;
    }
    return -1;
  }
  // regular logbook stats update
  if (logbookTimer.isTimeout()) {
    publishLogbookStats();
    logbookTimer.increment();
  }
  return 0;
}

int Readout::_stop()
{

  theLog.log(LogInfoSupport_(3005), "Readout executing STOP");
  gReadoutStats.counters.state = stringToUint64("> stop");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  // execute custom command
  executeCustomCommand("preSTOP");

  // raise flag
  stopTimer.reset(cfgFlushEquipmentTimeout * 1000000); // add a delay before stopping aggregator - continune to empty FIFOs
  isRunning = 0;

  // disable data producers
  for (auto&& readoutDevice : readoutDevices) {
    readoutDevice->setDataOff();
  }

  // wait a bit and start flushing aggregator
  if (cfgFlushEquipmentTimeout > 0) {
    usleep(cfgFlushEquipmentTimeout * 1000000 / 2);
    agg->doFlush = true;
    theLog.log(LogInfoDevel, "Flushing aggregator");
  }

  // wait main thread completed
  if (runningThread != nullptr) {
    runningThread->join();
  }
  runningThread = nullptr;

  for (auto&& readoutDevice : readoutDevices) {
    readoutDevice->stop();
  }
  theLog.log(LogInfoDevel, "Readout stopped");

  theLog.log(LogInfoDevel, "Stopping aggregator");
  agg->stop();

  // wait a bit if some pending data pages still in use
  if (cfgFlushConsumerTimeout > 0) {
    theLog.log(LogInfoDevel, "Waiting max %fs for data pages in use",cfgFlushConsumerTimeout);
    stopTimer.reset(cfgFlushConsumerTimeout * 1000000);
    for(;;) {
      int totalPagesPending = 0;
      for (auto&& readoutDevice : readoutDevices) {
        size_t nPagesTotal = 0, nPagesFree = 0;
        if (readoutDevice->getMemoryUsage(nPagesFree, nPagesTotal) == 0) {
          totalPagesPending += nPagesTotal - nPagesFree;
        }
      }
      if (stopTimer.isTimeout() || (totalPagesPending == 0)) {
	break;
      }
      usleep(100000);
    }
  }

  theLog.log(LogInfoDevel, "Stopping consumers");
  // notify consumers of imminent data flow stop
  for (auto& c : dataConsumers) {
    c->stop();
  }

  // ensure output buffers empty ?

  // check status of memory pools
  for (auto&& readoutDevice : readoutDevices) {
    size_t nPagesTotal = 0, nPagesFree = 0, nPagesUsed = 0;
    if (readoutDevice->getMemoryUsage(nPagesFree, nPagesTotal) == 0) {
      nPagesUsed = nPagesTotal - nPagesFree;
      theLog.log(LogInfoDevel_(3003), "Equipment %s : %d/%d pages (%.2f%%) still in use", readoutDevice->getName().c_str(), (int)nPagesUsed, (int)nPagesTotal, nPagesUsed * 100.0 / nPagesTotal);
    }
  }

  // report log statistics
  theLog.log("Errors: %lu Warnings: %lu", theLog.getMessageCount(InfoLogger::Severity::Error), theLog.getMessageCount(InfoLogger::Severity::Warning));

  // publish final logbook statistics (synchronously with timeout)
  publishLogbookStats(3000);

  // publish some final counters
  theLog.log(LogInfoDevel_(3003), "Final counters: timeframes = %" PRIu64 " readout = %s recorded = %s",
    gReadoutStats.counters.numberOfSubtimeframes.load(),
    NumberOfBytesToString(gReadoutStats.counters.bytesReadout.load(), "bytes").c_str(),
    NumberOfBytesToString(gReadoutStats.counters.bytesRecorded.load(),"bytes").c_str()
  );

  // execute custom command
  executeCustomCommand("postSTOP");

  theLog.log(LogInfoSupport_(3005), "Readout completed STOP");
  gReadoutStats.counters.state = stringToUint64("ready");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  return 0;
}

int Readout::_reset()
{
  // reset run number for logs
  theLogContext.setField(InfoLoggerContext::FieldName::Run, "");
  theLog.setContext(theLogContext);

  theLog.log(LogInfoSupport_(3005), "Readout executing RESET");
  gReadoutStats.counters.state = stringToUint64("> reset");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  // reset error flags
  isError = 0;
  logFirstError = 0 ;

  // close consumers before closing readout equipments (owner of data blocks)
  theLog.log(LogInfoDevel, "Releasing primary consumers");
  for (unsigned int i = 0; i < dataConsumers.size(); i++) {
    if (!dataConsumers[i]->isForwardConsumer) {
      theLog.log(LogInfoDevel, "Releasing consumer %s", dataConsumers[i]->name.c_str());
      dataConsumers[i] = nullptr;
    }
  }
  theLog.log(LogInfoDevel, "Releasing secondary consumers");
  for (unsigned int i = 0; i < dataConsumers.size(); i++) {
    if (dataConsumers[i] != nullptr) {
      theLog.log(LogInfoDevel, "Releasing consumer %s", dataConsumers[i]->name.c_str());
      dataConsumers[i] = nullptr;
    }
  }
  dataConsumers.clear();

  theLog.log(LogInfoDevel, "Releasing aggregator");
  if (agg != nullptr) {
    agg_output->clear();
    agg = nullptr; // destroy aggregator, and release blocks it may still own.
  }

  // todo: check nothing in the input pipeline flush & stop equipments
  for (auto&& readoutDevice : readoutDevices) {
    // ensure nothing left in output FIFO to allow releasing memory
    // printf("readout: in=%llu out=%llu\n",readoutDevice->dataOut->getNumberIn(),readoutDevice->dataOut->getNumberOut());
    theLog.log(LogInfoDevel, "Releasing equipment %s", readoutDevice->getName().c_str());
    readoutDevice->dataOut->clear();
  }

  // printf("agg: in=%llu out=%llu\n",agg_output.getNumberIn(),agg_output.getNumberOut());

  theLog.log(LogInfoDevel, "Releasing readout devices");
  for (size_t i = 0, size = readoutDevices.size(); i != size; ++i) {
    if (readoutDevices[i] != nullptr) {
      // ensure readout equipment threads stopped before releasing resources
      readoutDevices[i]->abortThread();
    }
    readoutDevices[i] = nullptr; // effectively deletes the device
  }
  readoutDevices.clear();

  // reset memory manager
  theLog.log(LogInfoDevel, "Releasing memory bank manager");
  theMemoryBankManager.reset();

  // closing latency file
  if (latencyFd >= 0) {
    close(latencyFd);
    latencyFd = -1;
  }

#ifdef WITH_LOGBOOK
  // closing logbook
  theLogbookThread = nullptr;
#endif

#ifdef WITH_ZMQ
  // close tfServer
  tfServer = nullptr;
#endif

  theLog.log(LogInfoSupport_(3005), "Readout completed RESET");
  gReadoutStats.reset();
  gReadoutStats.counters.state = stringToUint64("standby");
  gReadoutStats.counters.notify++;
  gReadoutStats.publishNow();

  return 0;
}

Readout::~Readout() {
  if (cfgVerbose) {
    theLog.log(LogInfoDevel, "Releasing resources");
  }

  // in case some components still active, cleanup in order
  if (runningThread != nullptr) {
    stopTimer.reset(0);
    isRunning=0;
    runningThread->join();
  }
  agg = nullptr;
  agg_output = nullptr;
  // ensure readout equipment threads stopped before releasing resources
  for (const auto &d : readoutDevices) {
    d->abortThread();
  }
  // after aggregator, because they own the data blocks
  dataConsumers.clear();
  readoutDevices.clear();

  if (customCommandsShellPid) {
    if (cfgVerbose) {
      theLog.log(LogInfoDevel_(3013), "Closing custom command shell");
    }
    if (customCommandsShellFdIn >= 0) {
      close(customCommandsShellFdIn);
    }
    if (customCommandsShellFdOut >= 0) {
      close(customCommandsShellFdOut);
    }
    pid_t pidDone = 0;
    int pidStatus;
    for (int i=0; i<100; i++) {
      pidDone = waitpid(customCommandsShellPid, &pidStatus, WNOHANG);
      if (pidDone) {
        break;
      }
      usleep(10000);
    }
    if (pidDone != customCommandsShellPid) {
      theLog.log(LogInfoDevel_(3013), "Killing %d", (int)customCommandsShellPid);
      kill(customCommandsShellPid, SIGKILL);
    }
    if (customCommandsShellFdErr >= 0) {
      customCommandsShellLog = nullptr;
      close(customCommandsShellFdErr);
    }
  }

  #ifdef WITH_DB
  dbHandle = nullptr;
  #endif
}

void Readout::executeCustomCommand(const char *stateChange) {
  if (customCommandsShellPid) {
    auto it = customCommands.find(stateChange);
    if (it != customCommands.end()) {
      theLog.log(LogInfoDevel_(3013), "Executing custom command for %s : %s", it->first.c_str(), it->second.c_str());
      std::string cmd = it->second + "\n";
      auto retval = write(customCommandsShellFdIn, cmd.c_str(), cmd.length());
      (void)retval;
      fsync(customCommandsShellFdIn);
      LineBuffer b;
      const int cmdTimeout = 10000; // 10s timeout
      if (b.appendFromFileDescriptor(customCommandsShellFdOut, cmdTimeout)) {
        theLog.log(LogInfoDevel_(3013), "Command launcher unavailable"); // EOF
      }
      std::string result;
      if (b.getNextLine(result) == 0) {
        theLog.log(LogInfoDevel_(3013), "Command executed: %s", result.c_str());
      } else {
        theLog.log(LogInfoDevel_(3013), "Unknown command result");
      }
    }
  }
}

#ifdef WITH_OCC
class ReadoutOCCStateMachine : public RuntimeControlledObject
{
 public:
  ReadoutOCCStateMachine(std::unique_ptr<Readout> r) : RuntimeControlledObject("Readout Process")
  {
    theReadout = std::move(r);
    // the following does not work: getRole() is empty at this stage - BUG. O2_ROLE is defined.
    // occRole = this->getRole();
  }

  int executeConfigure(const boost::property_tree::ptree& properties)
  {
    if (theReadout == nullptr) {
      return -1;
    }

    if (this->getRole() != occRole) {
      theLog.log(LogWarningDevel_(3243), "OCC role mismatch: getRole()=%s %s=%s occRole=%s", this->getRole().c_str(), OCC_ROLE_ENV, getenv(OCC_ROLE_ENV), occRole.c_str());
    }

    return theReadout->configure(properties);
  }

  int executeReset()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    return theReadout->reset();
  }

  int executeRecover()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    return -1;
  }

  int executeStart()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    // set run number
    occRunNumber = this->getRunNumber();
    return theReadout->start();
  }

  int executeStop()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    int ret = theReadout->stop();
    // unset run number
    occRunNumber = 0;
    theLogContext.setField(InfoLoggerContext::FieldName::Run, "");
    theLog.setContext(theLogContext);
    unsetenv(envRunNumber);
    return ret;
  }

  int executePause()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    return -1;
  }

  int executeResume()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    return -1;
  }

  int executeExit()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    theReadout = nullptr;
    return 0;
  }

  int iterateRunning()
  {
    if (theReadout == nullptr) {
      return -1;
    }
    return theReadout->iterateRunning();
  }

  int iterateCheck()
  {
    //    printf("iterateCheck\n");
    if (theReadout == nullptr) {
      return 0;
    }
    return theReadout->iterateCheck();
  }

 private:
  std::unique_ptr<Readout> theReadout = nullptr;
};
#endif

// increment run number based on initial settings from env
void incrementRunNumber() {
  if (occRunNumber != undefinedRunNumber) {
    occRunNumber++;
  } else {
    if (getenv(envRunNumber) != nullptr) {
      occRunNumber = atoi(getenv(envRunNumber));
    }
  }
}

// the main program loop
int main(int argc, char* argv[])
{
  //printf("Starting %s - %d\n",argv[0], (int)getpid());
  
  // check environment settings

  // OCC control port. If set, use OCClib to handle Readout states.
  bool occMode = false;
  if (getenv(OCC_CONTROL_PORT_ENV) != nullptr) {
    occMode = true;
  }

  // flag to run readout states interactively from console
  bool interactiveMode = false;
  if (getenv("O2_READOUT_INTERACTIVE") != nullptr) {
    interactiveMode = true;
    occMode = false;
  }

  // set default role name
  const char *role = getenv(OCC_ROLE_ENV);
  if (role != nullptr) {
    occRole = role;
  } else {
    char hostname[128];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
      occRole = hostname + std::string(":") + std::to_string(getpid());
    }
  }
  
  // initialize logging
  theLogContext.setField(InfoLoggerContext::FieldName::Facility, "readout");
  theLog.setContext(theLogContext);
  for(const auto &c : readoutErrorCodes) {
    theLog.registerErrorCodes({ {std::get<0>(c),  std::get<1>(c) } });
  }

  // create readout instance
  std::unique_ptr<Readout> theReadout = std::make_unique<Readout>();
  int err = 0;

  // parse command line arguments
  err = theReadout->init(argc, argv);
  if (err) {
    return err;
  }

  if (occMode) {
#ifdef WITH_OCC
    theLog.log(LogInfoDevel, "Readout entering OCC state machine");
    ReadoutOCCStateMachine csm(std::move(theReadout));
    OccInstance occ(&csm);
    occ.wait();
#else
    theLog.log(LogErrorSupport_(3101), "OCC mode requested but not available in this build");
    return -1;
#endif
  } else if (interactiveMode) {
    theLog.log(LogInfoOps, "Readout entering interactive state machine");
    theLog.log(LogInfoOps, "(c) configure (s) start (t) stop (r) reset (r) recover (x) quit");

    enum class States { Undefined,
                        Standby,
                        Configured,
                        Running,
                        Error };
    enum class Commands { Undefined,
                          Configure,
                          Reset,
                          Start,
                          Stop,
                          Recover,
                          Exit };

    auto getStateName = [](States s) {
      switch (s) {
        case States::Undefined:
          return "undefined";
        case States::Standby:
          return "standby";
        case States::Configured:
          return "configured";
        case States::Running:
          return "running";
        case States::Error:
          return "error";
        default:
          break;
      }
      return "undefined";
    };

    auto getCommandName = [](Commands c) {
      switch (c) {
        case Commands::Undefined:
          return "undefined";
        case Commands::Configure:
          return "configure";
        case Commands::Start:
          return "start";
        case Commands::Stop:
          return "stop";
        case Commands::Reset:
          return "reset";
        case Commands::Recover:
          return "recover";
        case Commands::Exit:
          return "exit";
        default:
          break;
      }
      return "undefined";
    };

    States theState = States::Standby;
    Commands theCommand = Commands::Undefined;
    printf("State: %s\n", getStateName(theState));
    for (;;) {
      if (theCommand == Commands::Undefined) {
        int c = getchar();
        if (c > 0) {
          switch (c) {
            case 'c':
              theCommand = Commands::Configure;
              break;
            case 's':
              theCommand = Commands::Start;
              break;
            case 't':
              theCommand = Commands::Stop;
              break;
            case 'r':
              theCommand = Commands::Reset;
              break;
            case 'v':
              theCommand = Commands::Recover;
              break;
            case 'x':
              theCommand = Commands::Exit;
              break;
            default:
              break;
          }
        }
      }

      if (theCommand != Commands::Undefined) {
        printf("Executing %s\n", getCommandName(theCommand));
      }

      States newState = States::Undefined;
      bool isCommandValid = true;
      if (theState == States::Standby) {
        if (theCommand == Commands::Configure) {
          boost::property_tree::ptree properties; // an empty "extra" config
          err = theReadout->configure(properties);
          if (err) {
            newState = States::Error;
          } else {
            newState = States::Configured;
          }
        } else {
          isCommandValid = false;
        }
      } else if (theState == States::Configured) {
        if (theCommand == Commands::Start) {
          incrementRunNumber();
          err = theReadout->start();
          if (err) {
            newState = States::Error;
          } else {
            newState = States::Running;
          }
        } else if (theCommand == Commands::Reset) {
          err = theReadout->reset();
          if (err) {
            newState = States::Error;
          } else {
            newState = States::Standby;
          }
        } else {
          isCommandValid = false;
        }
      } else if (theState == States::Running) {
        if (theCommand == Commands::Stop) {
          err = theReadout->stop();
          if (err) {
            newState = States::Error;
          } else {
            newState = States::Configured;
          }
        } else {
          isCommandValid = false;
        }
      } else if (theState == States::Error) {
        if (theCommand == Commands::Reset) {
          err = theReadout->reset();
          if (err) {
            newState = States::Error;
          } else {
            newState = States::Standby;
          }
        } else {
          isCommandValid = false;
        }
      }

      if (theCommand == Commands::Exit) {
        break;
      }

      if (newState != States::Undefined) {
        printf("State: %s\n", getStateName(newState));
        theState = newState;
      }
      if ((theCommand != Commands::Undefined) && (!isCommandValid)) {
        printf("This command is invalid in current state\n");
      }

      theCommand = Commands::Undefined;

      if (theState == States::Running) {
        err = theReadout->iterateRunning();
        if (err == 1) {
          theLog.log(LogInfoSupport, "Readout requesting to stop");
          theCommand = Commands::Stop;
        } else if (err != 0) {
          theLog.log(LogErrorSupport_(3231), "Readout reported an error while running");
          theCommand = Commands::Stop;
        }
        err = theReadout->iterateCheck();
        if (err) {
          theLog.log(LogErrorSupport_(3231), "Readout reported an error");
          theCommand = Commands::Stop;
        }
      } else {
        usleep(100000);
      }
    }

  } else {
    theReadout->standaloneMode = true;
    theLog.log(LogInfoDevel, "Readout entering standalone state machine");
    boost::property_tree::ptree properties; // an empty "extra" config
    err = theReadout->configure(properties);
    if (err) {
      return err;
    }

    int nloop = theReadout->cfgNumberOfRuns; // number of start/stop loop to execute
    theLog.log("Will execute %d START/STOP cycle", nloop);

    auto logTimeGuard = [&](const std::string command, int t) {
      if (t) {
        time_t tt = t;
        struct tm* ts = localtime(&tt);
        theLog.log(LogInfoOps, "Readout will execute %s at %04d-%02d-%02d %02d:%02d:%02d", command.c_str(), ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec);
        if (t <= time(nullptr)) {
          theLog.log(LogWarningOps, "This date is in the past ! Will %s immediately", command.c_str());
        }
      }
    };

    // check START / STOP time
    logTimeGuard("START", theReadout->cfgTimeStart);
    logTimeGuard("STOP", theReadout->cfgTimeStop);

    // check START time
    while ((theReadout->cfgTimeStart > 0) && (time(nullptr) < theReadout->cfgTimeStart)) {
      if (ShutdownRequest) {
        nloop = 0;
        break;
      }
      usleep(5000);
    }

    // loop for testing, single iteration in normal conditions
    for (int i = 0; i < nloop; i++) {
      incrementRunNumber();
      err = theReadout->start();
      if (err) {
        return err;
      }
      while (1) {
        // check STOP time
        if (theReadout->cfgTimeStop) {
          if (time(nullptr) >= theReadout->cfgTimeStop) {
            break;
          }
        }

        err = theReadout->iterateRunning();
        if (err == 1) {
          theLog.log(LogInfoSupport, "Readout requesting to stop");
          break;
        } else if (err != 0) {
          theLog.log(LogErrorSupport_(3231), "Readout reported an error while running");
          break;
        }
        err = theReadout->iterateCheck();
        if (err) {
          theLog.log(LogErrorSupport_(3231), "Readout reported an error");
          break;
        }
      }
      err = theReadout->stop();
      if (err) {
        return err;
      }
    }
    err = theReadout->reset();
    if (err) {
      return err;
    }
  }

  gReadoutStats.counters.state = stringToUint64("> exit");
  gReadoutStats.counters.notify++;
  gReadoutStats.stopPublish();

  theReadout = nullptr;

  #ifdef WITH_DB
  mysql_library_end();
  #endif

  theLog.log(LogInfoSupport_(3001), "Readout process exiting");
  return 0;
}

