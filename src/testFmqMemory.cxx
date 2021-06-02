#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <fairmq/FairMQDevice.h>
#include <fairmq/FairMQTransportFactory.h>
//#include <fairmq/tools/Unique.h>

#define GB *1073741824LLU
#define SLEEPTIME 3
#define WAITHERE printf("Waiting %ds ",SLEEPTIME); for(int k=0; k<SLEEPTIME; k++) {printf(".");fflush(stdout);usleep(1000000);} printf("\n");


int main(int argc, const char* argv[]) {

  unsigned int ngb = 1;
  if (argc>=2) {
    ngb=(unsigned int)atoi(argv[1]);
  } else {
    printf("Usage: %s numberOfGigabytes\n",argv[0]);
    return -1;
  }

  std::unique_ptr<FairMQChannel> sendingChannel;
  std::shared_ptr<FairMQTransportFactory> transportFactory;
  FairMQUnmanagedRegionPtr memoryBuffer = nullptr;
  FairMQProgOptions fmqOptions;

  printf("Create FMQ channel\n");
  // random name: use fair::mq::tools::Uuid()
  transportFactory = FairMQTransportFactory::CreateTransportFactory("shmem", "readout-test", &fmqOptions);
  sendingChannel = std::make_unique<FairMQChannel>("readout-test", "pair", transportFactory);
  WAITHERE;

  printf("Get unmanaged memory\n");
  long long mMemorySize = ngb GB;
  memoryBuffer = sendingChannel->Transport()->CreateUnmanagedRegion(mMemorySize, [](void* /*data*/, size_t /*size*/, void* /*hint*/) {});
  if (memoryBuffer==nullptr) { printf("Failed to get buffer\n"); return 1; }
  printf("Got %p : %llu\n", memoryBuffer->GetData(), (unsigned long long)memoryBuffer->GetSize());
  WAITHERE;

  printf("Write to memory\n");
  for (unsigned int i=0; i<ngb; i++) {
    char *ptr=(char *)memoryBuffer->GetData();
    ptr=&ptr[i GB];
    printf("#%u : writing @%p\n",i+1, ptr);
    memset(ptr,0,1 GB);
  }
  printf("Done writing\n");
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
