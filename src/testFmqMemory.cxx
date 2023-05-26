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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <chrono>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdarg.h>

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQTransportFactory.h>
//#include <fairmq/tools/Unique.h>

// simple log function with good timestamp
void log(const char *message, ...) {
	char buffer[1024] = "";
	size_t len = 0;
	va_list ap;
	va_start(ap, message);
	vsnprintf(&buffer[len], sizeof(buffer), message, ap);
	va_end(ap);
        using namespace std::chrono;
	uint64_t now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	std::time_t seconds = now / 1000;
	int milliseconds = now % 1000;
	std::ostringstream oss;
	oss << std::put_time(std::gmtime(&seconds), "%Y-%m-%d %H:%M:%S.") << std::setfill('0') << std::setw(3) << milliseconds;
	std::string timestamp(oss.str());
	printf("%s\t%s\n", timestamp.c_str(), buffer);
        fflush(stdout);
}

// print process memory stats from /proc
void logMemoryUsage() {
  double memPageSize = getpagesize() / (1024.0*1024.0);
  const int maxpath = 256;
  char fn[maxpath];
  snprintf(fn, maxpath, "/proc/%d/statm", getpid());
  FILE *fp=fopen(fn,"r");
  const int maxline = 256;
  char buf[maxline];
  if (fgets(buf, maxline, fp) != NULL) {
    int vsize, vresident, vshared, vtext, vlib, vdata, vdt;
    if (sscanf(buf, "%d %d %d %d %d %d %d", &vsize, &vresident, &vshared, &vtext, &vlib, &vdata, &vdt) == 7) {    
      log("Memory stats: size = %6.2f MB\tresident = %6.2f MB\tshared = %6.2f MB", vsize * memPageSize, vresident * memPageSize, vshared * memPageSize);
    }
  }
  fclose(fp);
}

#define GB *1073741824LLU
#define SLEEPTIME 3
//#define WAITHERE printf("Waiting %ds ",SLEEPTIME); for(int k=0; k<SLEEPTIME; k++) {printf(".");fflush(stdout);usleep(1000000);} printf("\n");
#define WAITHERE logMemoryUsage();


// memory settings
int memLock = 0;	// lock the whole process memory
int memZero = 2;	// write mode: 0=no write 1=memset 2=bzero 3=1 byte per page
int fmqMemLock = 1;	// lock FMQ region
int fmqMemZero = 0;	// zero FMQ region
int nLoops = 1;         // number of test loops
int memWaitRelease = 0;	// amount of time to keep the memory before releasing it

int main(int argc, char* argv[]) {

  unsigned int ngb = 1;
  int syncTime = 0;
  if (argc>=2) {
    ngb=(unsigned int)atoi(argv[1]);
  } else {
    printf("Usage: %s numberOfGigabytes\n",argv[0]);
    return -1;
  }
  // other options
  for (int i = 2; i < argc; i++) {
    char *k = argv[i];
    char *v = strchr(k, '=');
    if (v != nullptr) {
      *v = 0;
      v++;
    }
    if (strcmp(k, "syncTime") == 0) {
      syncTime = atoi(v);
    } else if (strcmp(k, "memLock") == 0) {
      memLock = atoi(v);
    } else if (strcmp(k, "memZero") == 0) {
      memZero = atoi(v);
    }  else if (strcmp(k, "fmqMemLock") == 0) {
      fmqMemLock = atoi(v);
    } else if (strcmp(k, "fmqMemZero") == 0) {
      fmqMemZero = atoi(v);
    } else if (strcmp(k, "nLoops") == 0) {
      nLoops = atoi(v);
    } else if (strcmp(k, "memWaitRelease") == 0) {
      memWaitRelease = atoi(v);
    } else {
      printf("unknown option %s\n", k);
      return -1;
    }
  }
  // wait until scheduled startup time (given modulo round number of seconds)
  if (syncTime>0) {
    time_t t = time(NULL);
    time_t waitT = syncTime - (t % syncTime);
    log("Waiting sync time (%ds)", (int)waitT);
    t = t + waitT;
    while (time(NULL) != t) {
      usleep(10000);
    }
  }

  log("Locking process memory: %s", memLock ? "yes" : "no");
  if (memLock) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      log("failed to lock memory");
    }
  }

  log("Startup pid %d",(int)getpid());
  WAITHERE;

  for(int nn=0; nn < nLoops; nn++) {
  log("Starting test loop %d / %d", nn + 1, nLoops);
  WAITHERE;

  std::unique_ptr<FairMQChannel> sendingChannel;
  std::shared_ptr<FairMQTransportFactory> transportFactory;
  FairMQUnmanagedRegionPtr memoryBuffer = nullptr;
  FairMQProgOptions fmqOptions;

  log("Create FMQ channel");
  // random name: use fair::mq::tools::Uuid()
  transportFactory = FairMQTransportFactory::CreateTransportFactory("shmem", "readout-test", &fmqOptions);
  sendingChannel = std::make_unique<FairMQChannel>("readout-test", "pair", transportFactory);
  WAITHERE;

  log("Get unmanaged memory (lock=%s, zero=%s)", fmqMemLock ? "yes" : "no", fmqMemZero ? "yes" : "no");
  long long mMemorySize = ngb GB;
  auto t00 = std::chrono::steady_clock::now();
  try {
//  memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {});
  memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {},
  "",0,fair::mq::RegionConfig{(bool)fmqMemLock, (bool)fmqMemZero}); // lock / zero
  }
  catch(...) {
    log("Failed to get buffer (exception)"); return 1;
  }
  if (memoryBuffer==nullptr) { log("Failed to get buffer"); return 1; }
  memoryBuffer->SetLinger(1);
  std::chrono::duration<double> tdiff0 = std::chrono::steady_clock::now() - t00;
  log("Got %p : %llu - %.2lf GB/s", memoryBuffer->GetData(), (unsigned long long)memoryBuffer->GetSize(), ngb * 1.0/tdiff0.count());
  WAITHERE;

  if (memZero) {
    log("Write to memory (mode %d), by chunks of 1GB", memZero);
    t00 = std::chrono::steady_clock::now();
    for (unsigned int i=0; i<ngb; i++) {
      auto t0 = std::chrono::steady_clock::now();
      char *ptr=(char *)memoryBuffer->GetData();
      ptr=&ptr[i GB];
      printf("#%u : writing @%p ... ",i+1, ptr);
      if (memZero == 1) {
        memset(ptr,0,1 GB);
      } else if (memZero == 2) {
        bzero(ptr,1 GB);
      } else if (memZero == 3) {
        // marginally faster to write one byte per memory page
        for(size_t j=0; j<1 GB; j += getpagesize()) {
	  ptr[j]=0;
        }
      }
      std::chrono::duration<double> tdiff = std::chrono::steady_clock::now() - t0;
      printf(" %.2lf GB/s\n", 1.0/tdiff.count());
    }
    std::chrono::duration<double> tdiff1 = std::chrono::steady_clock::now() - t00;
    log("Done writing");
    log("Average: %.2lf GB/s (writing)", ngb * 1.0/tdiff1.count());
    log("Average: %.2lf GB/s (writing + malloc)", ngb * 1.0/(tdiff0.count() + tdiff1.count()));
    WAITHERE;
  }

  if (memWaitRelease) {
    log("Waiting %ds before releasing", memWaitRelease);
    sleep(memWaitRelease);
  }

  log("Cleanup FMQ unmanaged region");
  memoryBuffer = nullptr;
  WAITHERE;

  log("Cleanup FMQ channel");
  sendingChannel.reset();
  transportFactory.reset();
  WAITHERE;

  log("Releasing FMQ variables");
  sendingChannel = nullptr;
  transportFactory = nullptr;
  WAITHERE;

  }

  log("Exit");
  return 0;
}

