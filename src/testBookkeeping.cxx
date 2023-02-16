#ifdef WITH_LOGBOOK
#include <BookkeepingApi/BkpClientFactory.h>
#endif

#include <stdio.h>
#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;

int main(int argc, char **argv) {
  #ifndef WITH_LOGBOOK
    printf("Bookkeeping library not found\n");
    return -1;
  #else

    setenv("O2_INFOLOGGER_MODE", "stdout", 1);
    InfoLogger theLog;

    const char *cfgLogbookUrl="localhost:4001";
      
    if (argc>1) {
      cfgLogbookUrl=argv[1];
    }

    try {
      theLog.log("Create handle");
      auto logbookHandle = o2::bkp::api::BkpClientFactory::create(cfgLogbookUrl);
      
      std::string occRole = "flp-test";
      unsigned int occRunNumber = 1;
    
      theLog.log("Updating");
      for (int i=0; i<10; i++) {      
        logbookHandle->flp()->updateReadoutCountersByFlpNameAndRunNumber(occRole, occRunNumber, i,i,i,i);
        sleep(1);
      }
      theLog.log("Done updating");
    }
    catch(const std::runtime_error &err) {
      theLog.log("Error: %s",err.what());
    }
  #endif
  return 0;
}
