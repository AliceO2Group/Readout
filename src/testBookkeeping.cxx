#ifdef WITH_LOGBOOK
#include <BookkeepingApiCpp/BookkeepingFactory.h>
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

    const char *cfgLogbookUrl="http://localhost:4000/api";
    const char *cfgLogbookApiToken="";
      
    if (argc>2) {
      cfgLogbookUrl=argv[1];
      cfgLogbookApiToken=argv[2];
    }

    std::unique_ptr<bookkeeping::BookkeepingInterface> logbookHandle;
    logbookHandle = bookkeeping::getApiInstance(cfgLogbookUrl, cfgLogbookApiToken);
      
    std::string occRole = "flp-test";
    unsigned int occRunNumber = 1;
    
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    logbookHandle->runStart(occRunNumber, now, now, "readout", RunType::TECHNICAL, 0, 0, 0, false, false, false, "normal");
    logbookHandle->flpAdd(occRole, "localhost", occRunNumber);
	
    theLog.log("Updating");
    for (int i=0; i<10; i++) {      
      logbookHandle->flpUpdateCounters(occRole, occRunNumber, i,i,i,i);
      sleep(1);
    }
    theLog.log("Done updating");
  
  #endif
  return 0;
}
