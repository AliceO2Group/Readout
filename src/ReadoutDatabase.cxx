#include "ReadoutDatabase.h"
#include <mysql.h>
#include <mysql/errmsg.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <string>
#include <map>
#include <time.h>

#if LIBMYSQL_VERSION_ID >= 80000
typedef bool my_bool;
#endif

int ReadoutDatabase::connect() {
  time_t now = time(NULL);
  if ((lastConnectTime != 0) && (now < lastConnectTime + reconnectTimeout)) return -2;
  lastConnectTime = now;
  if (db != nullptr) {
    mysql_close(db);
    db = nullptr;
  }
  db = mysql_init(nullptr);
  if (db == nullptr) {
    return -1;
  }
  if (mysql_real_connect(db,cxDbHost.c_str(),cxDbUser.c_str(),cxDbPwd.c_str(),cxDbName.c_str(),0,nullptr,0) == nullptr) {
    log(std::string("DB connect error :") + mysql_error(db));
    return -1;
  }
  log("DB connected");
  lastConnectTime = 0;
  return 0;
}

ReadoutDatabase::ReadoutDatabase(const char* cx, int v, const LogCallback& cb) {
  verbose = v;
  theLogCallback = cb;

  char *db_db=nullptr, *db_user=nullptr, *db_pwd=nullptr, *db_host=nullptr;
  char *p=nullptr,*ptr,*lptr;
    
  if (cx == nullptr) {
    throw __LINE__;
  }
  
  if (strlen(cx) == 0) {
    throw __LINE__;
  }

  db=mysql_init(nullptr);
  if (db == nullptr) {
    throw __LINE__;
  }

  p = strdup(cx);
  if (p == nullptr) {
    throw __LINE__;
  }
    
  ptr=p;

  // user
  for (lptr=ptr;*ptr!=0;ptr++) {
    if (*ptr==':') {
      db_user=lptr;
      *ptr=0;
      ptr++;
      break;
    }
  }
  cxDbUser = db_user;

  // pwd 
  for (lptr=ptr;*ptr!=0;ptr++) {
    if (*ptr=='@') {
      db_pwd=lptr;
      *ptr=0;
      ptr++;
      break;
    }
  }
  cxDbPwd = db_pwd;

  // host
  for (lptr=ptr;*ptr!=0;ptr++) {
    if (*ptr=='/') {
      db_host=lptr;
      *ptr=0;
      ptr++;      
      break;
    }
  }
  cxDbHost = db_host;

  // db name
  db_db=ptr;
  cxDbName = db_db;
  
  if ((db_user==nullptr)||(db_pwd==nullptr)||(db_host==nullptr)||(strlen(db_db)==0)) {
    goto open_failed;
  }

  log("Using database " + std::string(db_db) + "@" + std::string(db_host));

  // try to connect
  if (this->connect()) {
    goto open_failed;
  }

  free(p);  
  return;

open_failed:  
  if (p!=nullptr) free(p);
  if (db!=nullptr) {
    mysql_close(db);
    db=nullptr;
  }
  throw __LINE__;
  return;

}

ReadoutDatabase::~ReadoutDatabase() {
  if (db != nullptr) {
    mysql_close(db);
    db=nullptr;
    log("DB closed");
  }
}


int ReadoutDatabase::destroyTables() {
  return query(1, "drop table if exists stats_readout");
}

int ReadoutDatabase::clearTables() {
  return query(1, "truncate table stats_readout");
}
 
int ReadoutDatabase::createTables() {
  return query(1,
  "create table if not exists stats_readout ( \
  id INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT 'Unique row identifier', \
  run INT UNSIGNED NOT NULL COMMENT 'Run number' , \
  flp char(32) NOT NULL COMMENT 'FLP participating in run', \
  numberOfSubtimeframes BIGINT UNSIGNED DEFAULT 0 COMMENT 'Number of subtimeframes readout', \
  bytesReadout BIGINT UNSIGNED DEFAULT 0 COMMENT 'Number of bytes readout', \
  bytesRecorded BIGINT UNSIGNED DEFAULT 0 COMMENT 'Number of bytes recorded', \
  bytesFairMQ BIGINT UNSIGNED DEFAULT 0 COMMENT 'Number of bytes injected in FairMQ / DataDistribution', \
  time_update TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT 'Last update time', \
  primary key (id), \
  unique(run,FLP), \
  index(run), \
  index(FLP) \
  ) ENGINE = INNODB, COMMENT 'FLP readout statistics in a run'; "
  );
}


#define MAX_QUERY_LEN 1024
int ReadoutDatabase::query(int maxRetry, const char *inQuery,...) {
  va_list     ap;
  char query[MAX_QUERY_LEN];

  va_start(ap, inQuery);
  vsnprintf(query, sizeof(query), inQuery, ap);
  va_end(ap);
  
  lastQuery = query;
  lastError.clear();
  
  if (strlen(query)>=MAX_QUERY_LEN - 1) {
    lastError = "Query truncated";
    return -1;
  }
  
  log("Executing query: "  + std::string(query));
  
  if (maxRetry <= 0) {
    maxRetry = 1;
  }

  int i;
  for (i=1; i<=maxRetry; i++) {
    if (mysql_query(db,query) == 0)  break;
    lastError = std::string("DB query error :") + mysql_error(db);
    log("DB error: " + std::to_string(mysql_errno(db)) + " = " + lastError);
    if (mysql_errno(db) == CR_SERVER_LOST) {
      log("DB trying to reconnect");
      int err = connect();
      if (err == 0) {
        continue;
      } else {
        if (err == -2) {
	  log("DB reconnect - need to wait a bit before retry");
	}
        return -1;
      }
    }
    usleep(retryTimeout);
  }
  if (i > maxRetry) {
    return -1;
  }
  log("DB query success");
  return 0;
}

int ReadoutDatabase::dumpTablesContent() {

  if (query(1, "select * from stats_readout order by run, flp") != 0) {return __LINE__;}
  MYSQL_RES *res;
  res=mysql_store_result(db);
  if (res == nullptr) {return __LINE__;}

  MYSQL_ROW row;
  // unsigned int nFields;
  // nFields = mysql_num_fields(res);
  bool isFirst = 1;
  
  while ((row = mysql_fetch_row(res))) {
    unsigned long *lengths;
    lengths = mysql_fetch_lengths(res);

    MYSQL_FIELD *field;
    if (isFirst) {      
      for(unsigned int i = 0; (field = mysql_fetch_field(res)) ; i++) {
	if ((i>=1) && (i<=6)) {
          printf("%s   \t", field->name);
	}
      }
      printf("\n");
      isFirst = 0;
    }
    
    for(unsigned int i = 1; i <= 6; i++) {
	printf("%.*s\t", (int) lengths[i], row[i] ? row[i] : "NULL");
    }
    printf("\n");
  }

  return 0;
}


int ReadoutDatabase::dumpTablesStatus() {

  struct tableStatus{
    double sizeMB;
    unsigned long nRows;
  };
  
  std::map<std::string, tableStatus> tablesSummary;
  
  if (query(1, "\
    SELECT \
      TABLE_NAME AS `Table`, \
      ROUND((DATA_LENGTH + INDEX_LENGTH) / 1024 / 1024) AS `Size (MB)` \
    FROM \
      information_schema.TABLES \
    WHERE \
      TABLE_SCHEMA = '%s' \
    ORDER BY \
      (DATA_LENGTH + INDEX_LENGTH) \
    DESC; \
    ", cxDbName.c_str()) != 0) {return __LINE__;}

  MYSQL_RES *res;
  res=mysql_store_result(db);
  if (res == nullptr) {return __LINE__;}
  MYSQL_ROW row;   
  while ((row = mysql_fetch_row(res))) {
    if ((row[0] != nullptr) && (row[1]!=nullptr)) {
      tablesSummary[row[0]]= {atof(row[1]), 0};
    }
  }
  mysql_free_result(res);
  
  for(auto &s : tablesSummary) {
    if (query(1, "select count(*) from %s", s.first.c_str()) == 0) {
      MYSQL_RES *res;
      res=mysql_store_result(db);
      if (res != nullptr) {
        MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if (row != nullptr) {
          s.second.nRows = atol(row[0]);
	}
        mysql_free_result(res);
      }
    }
  }
  
  printf("           Table     Size (MB)         Rows\n");
  for(auto &s : tablesSummary) {
    printf("%16s%14.2f%14lu\n",s.first.c_str(),s.second.sizeMB, s.second.nRows);
  }

  return 0;
}

int ReadoutDatabase::updateRunCounters(
  uint64_t numberOfSubtimeframes, uint64_t bytesReadout, uint64_t bytesRecorded, uint64_t bytesFairMQ
  ) {

  return query(maxRetry,
    "UPDATE stats_readout set numberOfSubtimeframes = '%" PRIu64 "', bytesReadout = '%" PRIu64 "', bytesRecorded = '%" PRIu64 "', bytesFairMQ = '%" PRIu64 "' where run = '%" PRIu64 "' and flp = '%s'",
    numberOfSubtimeframes, bytesReadout, bytesRecorded, bytesFairMQ, vRun, vRole.c_str());
}

int ReadoutDatabase::initRunCounters(
  const char *flpName, uint64_t runNumber
) {

  vRun = runNumber;
  vRole = flpName;
    
  return query(maxRetry, "REPLACE INTO stats_readout(run, flp, numberOfSubtimeframes, bytesReadout, bytesRecorded, bytesFairMQ ) values ('%" PRIu64 "', '%s', default, default, default, default)", vRun, vRole.c_str());
}

const char* ReadoutDatabase::getError() {
  return lastError.c_str();
}

const char* ReadoutDatabase::getQuery() {
  return lastQuery.c_str();
}

void ReadoutDatabase::log(const std::string &msg) {
  if (verbose) {
    if (theLogCallback!=nullptr) {
      theLogCallback(msg);
    } else {
      printf("%s\n", msg.c_str());
    }
  }
}
