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

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQTransportFactory.h>
//#include <fairmq/tools/Unique.h>

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
      printf("Memory stats: size = %6.2f MB\tresident = %6.2f MB\tshared = %6.2f MB\n", vsize * memPageSize, vresident * memPageSize, vshared * memPageSize);
    }
  }
  fclose(fp);
}


#define GB *1073741824LLU
#define SLEEPTIME 3
//#define WAITHERE printf("Waiting %ds ",SLEEPTIME); for(int k=0; k<SLEEPTIME; k++) {printf(".");fflush(stdout);usleep(1000000);} printf("\n");
#define WAITHERE logMemoryUsage();


// memory settings
const bool memlock = false;	// lock the whole process memory
const bool fmqMemLock = true;	// lock FMQ region
const bool fmqMemZero = false;	// zero FMQ region


int main(int argc, const char* argv[]) {

  unsigned int ngb = 1;
  if (argc>=2) {
    ngb=(unsigned int)atoi(argv[1]);
  } else {
    printf("Usage: %s numberOfGigabytes\n",argv[0]);
    return -1;
  }
  
  printf("Locking process memory: %s\n", memlock ? "yes" : "no");
  if (memlock) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      printf("failed to lock memory\n");
    }
  }

  printf("Startup pid %d\n",(int)getpid());
  WAITHERE;

  std::unique_ptr<FairMQChannel> sendingChannel;
  std::shared_ptr<FairMQTransportFactory> transportFactory;
  FairMQUnmanagedRegionPtr memoryBuffer = nullptr;
  FairMQProgOptions fmqOptions;

  printf("Create FMQ channel\n");
  // random name: use fair::mq::tools::Uuid()
  transportFactory = FairMQTransportFactory::CreateTransportFactory("shmem", "readout-test", &fmqOptions);
  sendingChannel = std::make_unique<FairMQChannel>("readout-test", "pair", transportFactory);
  WAITHERE;

  printf("Get unmanaged memory (lock=%s, zero=%s)\n", fmqMemLock ? "yes" : "no", fmqMemZero ? "yes" : "no");
  long long mMemorySize = ngb GB;
  auto t00 = std::chrono::steady_clock::now();
  try {
//  memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {});
  memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {},
  "",0,fair::mq::RegionConfig{fmqMemLock, fmqMemZero}); // lock / zero
  }
  catch(...) {
    printf("Failed to get buffer (exception)\n"); return 1; 
  }
  if (memoryBuffer==nullptr) { printf("Failed to get buffer\n"); return 1; }
  memoryBuffer->SetLinger(1);
  std::chrono::duration<double> tdiff0 = std::chrono::steady_clock::now() - t00;
  printf("Got %p : %llu - %.2lf GB/s\n", memoryBuffer->GetData(), (unsigned long long)memoryBuffer->GetSize(), ngb * 1.0/tdiff0.count());
  WAITHERE;

  printf("Write to memory, by chunks of 1GB\n");
  t00 = std::chrono::steady_clock::now();
  for (unsigned int i=0; i<ngb; i++) {
    auto t0 = std::chrono::steady_clock::now();
    char *ptr=(char *)memoryBuffer->GetData();
    ptr=&ptr[i GB];
    printf("#%u : writing @%p ... ",i+1, ptr);
    //memset(ptr,0,1 GB);
    //bzero(ptr,1 GB);
    // marginally faster to write one byte per memory page
    if (1) {
      for(size_t j=0; j<1 GB; j += getpagesize()) {
	ptr[j]=0;
      }
    }
    std::chrono::duration<double> tdiff = std::chrono::steady_clock::now() - t0;
    printf(" %.2lf GB/s\n", 1.0/tdiff.count());
  }
  std::chrono::duration<double> tdiff1 = std::chrono::steady_clock::now() - t00;
  printf("Done writing\n");
  printf("Average: %.2lf GB/s (writing)\n", ngb * 1.0/tdiff1.count());
  printf("Average: %.2lf GB/s (writing + malloc)\n", ngb * 1.0/(tdiff0.count() + tdiff1.count()));
  WAITHERE;

  printf("Cleanup FMQ unmanaged region\n");
  memoryBuffer = nullptr;
  WAITHERE;

  printf("Cleanup FMQ channel\n");
  sendingChannel.reset();
  transportFactory.reset();
  WAITHERE;

  printf("Exit\n");
  return 0;
}

