// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

// a simple test program to readout ROC card

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
InfoLogger theLog;
#include <Common/Timer.h>

#include "MemoryBankManager.h"

#include <ReadoutCard/ChannelFactory.h>
#include <ReadoutCard/DmaChannelInterface.h>
#include <ReadoutCard/Exception.h>
#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/Parameters.h>

#include <time.h>
#include <vector>

#include <sys/mman.h>

#ifdef WITH_NUMA
#include <numa.h>
#endif

#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

class ROCdevice {

public:
  ROCdevice(std::string id);
  ~ROCdevice();

  void start();
  int doLoop();
  void poll();
  void stop();

  size_t nBytes = 0;

private:
  std::string bankId = "testROC";
  size_t bankSize = 2 * 1024 * 1024 * 1024L;

  size_t memoryPoolNumberOfPages = 1000;
  size_t memoryPoolPageSize = 2 * 1024 * 1024;

  std::string cardId = "0:0.0";
  int cfgChannelNumber = 0;
  std::string cfgLinkMask = "0";

  int cfgGeneratorEnabled = 1;
  int cfgGeneratorDataSize = 8192;
  std::string cfgGeneratorLoopback = "INTERNAL";
  std::string cfgGeneratorPattern = "INCREMENTAL";
  int cfgGeneratorRandomSizeEnabled = 0;
  std::string cfgResetLevel = "INTERNAL";
  size_t superPageSize;

  std::shared_ptr<MemoryBank> bank;
  std::shared_ptr<MemoryPagesPool> mp;
  AliceO2::roc::ChannelFactory::DmaChannelSharedPtr channel;
  AliceO2::roc::Parameters params;
  AliceO2::Common::Timer t;
};

ROCdevice::ROCdevice(std::string id) {

  bankId += id;

  bank = getMemoryBank(bankSize, "MemoryMappedFile", bankId);
  theMemoryBankManager.addBank(bank);
  mp = theMemoryBankManager.getPagedPool(
      memoryPoolPageSize, memoryPoolNumberOfPages, bankId); // pool of pages

  superPageSize = mp->getPageSize();
  superPageSize -=
      superPageSize % (32 * 1024); // must be a multiple of 32Kb for ROC

  cardId = id;
  params.setCardId(AliceO2::roc::Parameters::cardIdFromString(cardId));
  params.setChannelNumber(cfgChannelNumber);
  params.setGeneratorEnabled(cfgGeneratorEnabled);
  if (cfgGeneratorEnabled) {
    params.setGeneratorDataSize(cfgGeneratorDataSize);
    params.setGeneratorLoopback(
        AliceO2::roc::LoopbackMode::fromString(cfgGeneratorLoopback));
    params.setGeneratorPattern(
        AliceO2::roc::GeneratorPattern::fromString(cfgGeneratorPattern));
    params.setGeneratorRandomSizeEnabled(cfgGeneratorRandomSizeEnabled);
  }

  params.setBufferParameters(AliceO2::roc::buffer_parameters::Memory{
      mp->getBaseBlockAddress(), mp->getBaseBlockSize()});

  params.setLinkMask(AliceO2::roc::Parameters::linkMaskFromString(cfgLinkMask));

  channel = AliceO2::roc::ChannelFactory().getDmaChannel(params);
  channel->resetChannel(AliceO2::roc::ResetLevel::fromString(cfgResetLevel));

  std::string infoPciAddress = channel->getPciAddress().toString();
  int infoNumaNode = channel->getNumaNode();
  std::string infoSerialNumber = "unknown";
  auto v_infoSerialNumber = channel->getSerial();
  if (v_infoSerialNumber) {
    infoSerialNumber = std::to_string(v_infoSerialNumber.get());
  }
  std::string infoFirmwareVersion =
      channel->getFirmwareInfo().value_or("unknown");
  std::string infoCardId = channel->getCardId().value_or("unknown");
  theLog.log("ROC PCI %s @ NUMA node %d, serial number %s, firmware version "
             "%s, card id %s",
             infoPciAddress.c_str(), infoNumaNode, infoSerialNumber.c_str(),
             infoFirmwareVersion.c_str(), infoCardId.c_str());
}

ROCdevice::~ROCdevice() {}

void ROCdevice::start() {
  // start DMA
  theLog.log("Starting DMA for ROC %s:%d", cardId.c_str(), cfgChannelNumber);
  channel->startDma();
  t.reset();

  // get FIFO depth (it should be fully empty when starting)
  int RocFifoSize = channel->getTransferQueueAvailable();
  theLog.log("ROC input queue size = %d pages", RocFifoSize);
}

void ROCdevice::stop() {
  double runningTime = t.getTime();

  // stop DMA
  channel->stopDma();

//#define BASE 1024L
#define BASE 1000L
  size_t GBunit = (BASE * BASE * BASE);
  double dataRate = (nBytes / runningTime) / GBunit;
  printf("Rate = %.3f GB/s (base 1000)\n", dataRate);
}

//#define LOGDEBUG

int ROCdevice::doLoop() {

  // first empty fifo from available pages
  int nPop = 0;
  while ((channel->getReadyQueueSize() > 0)) {
    auto superpage = channel->getSuperpage(); // this is the first superpage in
                                              // FIFO ... let's check its state
    if (superpage.isFilled()) {
      mp->releasePage(superpage.getUserData());
      channel->popSuperpage();
      nPop++;
      nBytes += superpage.getSize();
    } else {
      break;
    }
  }
#ifdef LOGDEBUG
  printf("%s pop %d\n", cardId.c_str(), nPop);
#endif

  // give free pages to the driver
  int nPush = 0;
  while (channel->getTransferQueueAvailable() != 0) {
    void *newPage = mp->getPage();
    if (newPage != nullptr) {
      AliceO2::roc::Superpage superpage;
      superpage.setOffset((char *)newPage - (char *)mp->getBaseBlockAddress());
      superpage.setSize(superPageSize);
      superpage.setUserData(newPage);
      channel->pushSuperpage(superpage);
      nPush++;
    } else {
#ifdef LOGDEBUG
      printf("%s need free page\n", cardId.c_str());
#endif
      break;
    }
  }
#ifdef LOGDEBUG
  printf("%s pushed %d\n", cardId.c_str(), nPush);
#endif

  // call periodic function
  channel->fillSuperpages();

  return nPush + nPop;
}

void ROCdevice::poll() {
  // call periodic function
  channel->fillSuperpages();
}

int main(int argc, char **argv) {

  mlockall(MCL_CURRENT | MCL_FUTURE);

  int numaNodeId = -1;
  int runningTime = 10;
  int sleepTime = 5000;

  int argMin = 1;

  if ((argMin + 1 < argc) && (!strcmp(argv[argMin], "numaNode"))) {
    numaNodeId = atoi(argv[++argMin]);
    argMin++;
  }
  if ((argMin + 1 < argc) && (!strcmp(argv[argMin], "runningTime"))) {
    runningTime = atoi(argv[++argMin]);
    argMin++;
  }
  if ((argMin + 1 < argc) && (!strcmp(argv[argMin], "sleepTime"))) {
    sleepTime = atoi(argv[++argMin]);
    argMin++;
  }

  // bind alloc/exec on numa node
  if (numaNodeId >= 0) {
#ifdef WITH_NUMA
    struct bitmask *nodemask;
    nodemask = numa_allocate_nodemask();
    if (nodemask == NULL) {
      return -1;
    }
    numa_bitmask_clearall(nodemask);
    numa_bitmask_setbit(nodemask, numaNodeId);
    numa_bind(nodemask);
    printf("Locked to numa node %d\n", numaNodeId);
#else
    printf("Can not set numaNode ... program compiled without NUMA support\n");
#endif
  }

  // settings
  int doSleep = 1;

  // try to prevent deep sleeps
  int maxLatency = 1;
  int fd = open("/dev/cpu_dma_latency", O_WRONLY);
  if (fd < 0) {
    perror("open /dev/cpu_dma_latency");
    return 1;
  }
  if (write(fd, &maxLatency, sizeof(maxLatency)) != sizeof(maxLatency)) {
    perror("write to /dev/cpu_dma_latency");
    return 1;
  }
  printf("Set maxLatency=%d in /dev/cpu_dma_latency\n", maxLatency);

  std::vector<ROCdevice> devices;

  for (int i = argMin; i < argc; i++) {
    devices.push_back(ROCdevice(argv[i]));
  }

  // synchronize with clock: start on next round(10s) to sync with possible
  // other instances
  printf("Starting in %d seconds\n", (int)(10 - time(NULL) % 10));
  for (;;) {
    if (time(NULL) % 10 == 0) {
      break;
    }
    usleep(1000);
  }

  for (auto &d : devices) {
    d.start();
  }

  AliceO2::Common::Timer t;
  t.reset(runningTime * 1000000);
  unsigned long long nloop = 0;
  unsigned long long nsleep = 0;
  for (;;) {
    nloop++;
    int n = 0;
    for (auto &d : devices) {
      n += d.doLoop();
    }
    int minItems = 0;

    if (doSleep) {
      if ((n <= minItems) && 1) {
        nsleep++;
        usleep(sleepTime);
      }
    }

    if (0) {

      // these were other means to sleep

      AliceO2::Common::Timer tp;
      tp.reset(sleepTime);
      int k = 0;
      while (!tp.isTimeout()) {

        //      for (auto &d : devices) {
        //       d.poll();
        //      }
        //        usleep(sleepTime/5);
        //        continue;

        /*
                struct timespec req;
                req.tv_sec=sleepTime/1000000;
                req.tv_nsec=(sleepTime%1000000)*1000;

                int err=0;
                err=nanosleep(&req, NULL);
                if (err) {
                 printf("sleep interrupted - err %d\n",err);
                }
        */

        //        sched_yield();
        //        pthread_yield();

        /*
                struct timeval tv;
                tv.tv_sec=sleepTime/1000000;
                tv.tv_usec=(sleepTime%1000000);
                select(0,NULL,NULL,NULL,&tv);
        */

        if (doSleep) {
          usleep(sleepTime / 5);
        }

        k++;
      }
      double tt;
      tt = tp.getTime();
      // printf("Sleep x%d = %f\n",k,tt);
      if (tt * 1000000 > sleepTime * 2) {
        printf("Sleep too much: %f\n", tt);
      }
    }

    if (t.isTimeout()) {
      break;
    }
  }

  for (auto &d : devices) {
    d.stop();
  }
  printf("nloop=%llu nsleep=%llu ratio=%.3f\n", nloop, nsleep,
         nsleep * 1.0 / nloop);
  return 0;
}
