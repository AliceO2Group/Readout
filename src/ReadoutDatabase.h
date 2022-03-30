#include <mysql.h>
#include <mysqld_error.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <functional>

class ReadoutDatabase {
public:
  // an optional user-provided logging function for all DB-related ops
  typedef std::function<void(const std::string &)> LogCallback;

  ReadoutDatabase(const char* cx, int verbose = 0, const LogCallback& cb = nullptr); // create a handle to DB. Connection string in the form user:pwd@host/dbname
  ~ReadoutDatabase();

  // admin database structure
  int createTables(); // create database tables
  int clearTables(); // delete content from all tables
  int destroyTables(); // destroy all tables

  // stats database
  int dumpTablesContent(); // dump database content
  int dumpTablesStatus(); // summarize database status (table content, etc)

  // populate database
  int initRunCounters( const char *flpName, uint64_t runNumber ); // initialize counters, once per run
  int updateRunCounters(
    uint64_t numberOfSubtimeframes, uint64_t bytesReadout, uint64_t bytesRecorded, uint64_t bytesFairMQ
    ); // update counters, need initCounters() first.

  // error handling
  const char* getError(); // get a description of last error, if any
  const char* getQuery(); // get a description of last query, if any
  
  // flag to control verbosity
  int verbose = 0;
  
private:
  MYSQL *db = nullptr; // handle to mysql db
  int query(int maxRetry, const char *inQuery, ...); // function to execute queries, printf-like format
 
  uint64_t vRun;
  std::string vRole;
  std::string cxDbName; // name of the database used
  
  int maxRetry = 20; // number of query retries
  int retryTimeout = 50; // retry interval (milliseconds)

  LogCallback theLogCallback;
  void log(const std::string &log);

  std::string lastError = ""; // error string of last query, if any
  std::string lastQuery = ""; // last query executed  
};
