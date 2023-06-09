#ifdef WITH_LOGBOOK
#include <BookkeepingApi/BkpClientFactory.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;

int main(int argc, char **argv) {
  #ifndef WITH_LOGBOOK
    printf("Bookkeeping library not found\n");
    return -1;
  #else

    setenv("O2_INFOLOGGER_MODE", "stdout", 1);
    InfoLogger theLog;

    // get options from command line
    std::string cfgLogbookUrl="localhost:4001"; // bookkeeping API server end-point
    int syncTime = 0; // startup synchronization
    std::string occRole = "flp-test";
    unsigned int occRunNumber = 1;
    int sleepTime = 1000; // sleep time between iterations (milliseconds)
    int nPerRun = 10; // iterations per run
    int nPerHost = 0; // iterations per host

    for (int i = 1; i < argc; i++) {
      char *k = argv[i];
      char *v = strchr(k, '=');
      if (v != nullptr) {
        *v = 0;
        v++;
      }
      if (strcmp(k, "cfgLogbookUrl") == 0) {
        cfgLogbookUrl = v;
      } else if (strcmp(k, "syncTime") == 0) {
        syncTime = atoi(v);
      } else if (strcmp(k, "occRole") == 0) {
        occRole = v;
      } else if (strcmp(k, "occRunNumber") == 0) {
        occRunNumber = atoi(v);
      } else if (strcmp(k, "sleepTime") == 0) {
        sleepTime = atoi(v);
      } else if (strcmp(k, "nPerRun") == 0) {
        nPerRun = atoi(v);
      } else if (strcmp(k, "nPerHost") == 0) {
        nPerHost = atoi(v);
      } else {
        printf("unknown option %s\n", k);
        return -1;
      }
    }
    // wait until scheduled startup time (given modulo round number of seconds)
    if (syncTime>0) {
      time_t t = time(NULL);
      time_t waitT = syncTime - (t % syncTime);
      theLog.log("Waiting sync time (%ds)", (int)waitT);
      t = t + waitT;
      while (time(NULL) != t) {
        usleep(10000);
      }
    }

    try {
      theLog.log("Create handle to %s", cfgLogbookUrl.c_str());
      auto logbookHandle = o2::bkp::api::BkpClientFactory::create(cfgLogbookUrl);

      theLog.log("Updating %s:%d (%d loops for %d hosts, %d ms sleep between each)", occRole.c_str(), (int)occRunNumber, nPerRun, nPerHost, sleepTime);
      for (int i=0; i<nPerRun; i++) {
        for (int k=1; k <= (nPerHost ? nPerHost : 1); k++) {
          char host[256];
          if (nPerHost>0) {
            snprintf(host,256,"%s-%03d", occRole.c_str(), k+1);
          } else {
            snprintf(host,256,"%s", occRole.c_str());
          }
          printf("%s : %d\n", host, i);
          logbookHandle->flp()->updateReadoutCountersByFlpNameAndRunNumber(host, occRunNumber, i,i,i,i);
          if (sleepTime>0) {
            usleep(sleepTime * 1000);
          }
        }
      }
      theLog.log("Done updating");
    }
    catch(const std::runtime_error &err) {
      theLog.log("Error: %s",err.what());
    }
  #endif
  return 0;
}
