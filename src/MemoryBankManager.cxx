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

#include "MemoryBankManager.h"
#include "ReadoutUtils.h"
#include <unistd.h>
#include <sys/mman.h>
#include <thread>
#include <Common/Timer.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef WITH_ZMQ
#include <zmq.h>
#endif

#include "readoutInfoLogger.h"

MemoryBankManager::MemoryBankManager() {
}

MemoryBankManager::~MemoryBankManager() {
  stopMonitoring();
}

int MemoryBankManager::addBank(std::shared_ptr<MemoryBank> bankPtr, std::string name)
{

  // disable concurrent execution of this function
  std::unique_lock<std::mutex> lock(bankMutex);

  try {
    if (name.length() == 0) {
      name = bankPtr->getDescription();
    }
    banks.push_back({ name, bankPtr, {} });
  } catch (...) {
    return -1;
  }

  return 0;
}

std::string MemoryBankManager::getMonitorFifoPath(int id) {
  if (id < 0) {
    return monitorPath;
  }
  char fn[128];
  snprintf(fn,sizeof(fn),"%s-%d", monitorPath.c_str(), id);
  return fn;
}

std::shared_ptr<MemoryPagesPool> MemoryBankManager::getPagedPool(size_t pageSize, size_t pageNumber, std::string bankName, size_t firstPageOffset, size_t blockAlign, int numaNode)
{

  void* baseAddress = nullptr; // base address of bank from which the block is taken
  size_t offset = 0;           // offset of new block (relative to baseAddress)
  size_t blockSize = 0;        // size of new block (in bytes)
  int newId = 0;

  // disable concurrent execution of this block
  // automatic release of lock when going out of scope
  // beginning of locked block
  {
    std::unique_lock<std::mutex> lock(bankMutex);

    if (banks.size() == 0) {
      theLog.log(LogErrorSupport_(3103), "Can not create memory pool: no memory bank defined");
      return nullptr;
    }

    // look for corresponding named bank
    // if not specified, used first one...
    unsigned int ix = 0;
    bool bankFound = false;
    if (bankName.size() > 0) {
      for (ix = 0; ix < banks.size(); ix++) {
        if (banks[ix].name == bankName) {
          bankFound = true;
          break;
        }
      }
    } else {
      if (banks.size()) {
        ix = 0;
        bankFound = true;
        theLog.log(LogInfoDevel_(3008), "Bank name not specified, using first one (%s)", banks[ix].name.c_str());
      }
    }
    if (!bankFound) {
      theLog.log(LogErrorSupport_(3103), "Can not find specified memory bank '%s'", bankName.c_str());
      return nullptr;
    }

    // theLog.log(LogDebugTrace_(3008),"Allocating %ld x %ld bytes from memory bank '%s'",pageNumber,pageSize,banks[ix].name.c_str());

    // reserve space from big block
    baseAddress = banks[ix].bank->getBaseAddress();
    offset = 0;
    blockSize = pageSize * (pageNumber + 1); // this is the maximum space to use... may loose some pages for alignment

    // alloc new block after existing ranges already in use
    for (auto it = banks[ix].rangesInUse.begin(); it != banks[ix].rangesInUse.end(); ++it) {
      size_t maxOffset = it->offset + it->size;
      if (maxOffset > offset) {
        offset = maxOffset;
      }
    }

    // align at least to memory page
    int systemPageSize = getpagesize();
    if ((int)blockAlign < systemPageSize) {
      blockAlign = systemPageSize;
      theLog.log(LogInfoDevel,"Aligning memory block by default on system page size = %d bytes", systemPageSize);
    }

    // align beginning of block as specified
    if (blockAlign > 0) {
      size_t bytesExcess = (((size_t)baseAddress) + offset) % blockAlign;
      if (bytesExcess) {
        size_t alignOffset = blockAlign - bytesExcess;
        offset += alignOffset;    // advance to next aligned address
        blockSize -= alignOffset; // decrease block size to respect initial limit
      }
    }

    // check not exceeding bank size
    if (offset + blockSize > banks[ix].bank->getSize()) {
      theLog.log(LogErrorSupport_(3230), "Not enough space left in memory bank '%s' (need %ld bytes more)", banks[ix].name.c_str(), offset + blockSize - banks[ix].bank->getSize());
      throw std::bad_alloc();
    }

    // keep track of this new block
    banks[ix].rangesInUse.push_back({ offset, blockSize });
    newId = ++poolIndex;
  }
  // end of locked block

  if (numaNode >= 0) {
      // actual memory assignment is done on first write, in particular for FMQ
      // so set NUMA node and zero the memory to lock it
      // or try to move the block ?
      numaBind(numaNode);
  }

  theLog.log(LogInfoDevel, "Zero memory");
  void *blockAddress = &(((char*)baseAddress)[offset]);
  // ensure pages stay in RAM
  #ifdef MLOCK_ONFAULT
    // only on Linux
    // no need to lock them now, we write the full range on next line. keep them locked then. this is faster.
    mlock2(blockAddress, blockSize, MLOCK_ONFAULT);
  #else
    mlock(blockAddress, blockSize);
  #endif
  const int nMemThreads = 1;
  if (nMemThreads <= 1) {
    bzero(blockAddress, blockSize);
  } else {
    // parallel
    std::thread memThreads[nMemThreads];
    char *ptr = (char *)blockAddress;
    char *ptrEnd = (char *)blockAddress + blockSize;
    const size_t blockUnit = 128 * 1024UL * 1024UL; // block unit = 128MB
    int nThreads = 0;
    for (int i = 0; i<nMemThreads; i++) {
      size_t sz = blockSize / nMemThreads;
      sz = sz + (blockUnit - ((size_t)ptr + sz) % blockUnit); // round up to next block
      if ((ptr + sz > ptrEnd) || (i + 1 == nMemThreads)) {
        sz = ptrEnd - ptr;
      }
      theLog.log(LogDebugDevel, "Thread %d  - zero %p - %llu", i, ptr, (unsigned long long)sz);
      memThreads[i] = std::thread(bzero, ptr, sz);
      ptr += sz;
      nThreads++;
      if (ptr >= ptrEnd) {
        break;
      }
    }
    for (int i = 0; i < nThreads; i++) {
      memThreads[i].join();
    }
  }
  theLog.log(LogInfoDevel, "Zero memory done");

  if (numaNode >= 0) {
    numaBind(-1);
  }

  int ptrNumaNode = -1;
  if (numaGetNodeFromAddress(blockAddress, ptrNumaNode) == 0) {
    theLog.log(LogInfoDevel, "Memory at %p is at node %d", blockAddress, ptrNumaNode);
    if (numaNode >= 0) {
      if (ptrNumaNode != numaNode) {
        theLog.log(LogWarningDevel, "Warning, could not allocate memory pool on requested NUMA node");
        // todo: try to move ?
      }
    }
  }

  // create pool of pages from new block
  std::shared_ptr<MemoryPagesPool> mpp;
  try {
    mpp = std::make_shared<MemoryPagesPool>(pageSize, pageNumber, &(((char*)baseAddress)[offset]), blockSize, nullptr, firstPageOffset, newId);
    if (mpp != nullptr) {
      // create FIFO for monitoring
      mkfifo(getMonitorFifoPath(newId).c_str(), S_IRUSR | S_IWUSR | S_IRGRP| S_IROTH);
      // keep reference to created pool for monitoring purpose
      std::unique_lock<std::mutex> lock(bankMutex);
      pools.push_back(mpp);
    }
  }
  catch (int err) {
    theLog.log(LogErrorSupport_(3230), "Can not create memory pool from bank: error %d", err);
  }
  catch (...) {
    theLog.log(LogErrorSupport_(3230), "Can not create memory pool from bank");
  }
  return mpp;
}

// a global MemoryBankManager instance
MemoryBankManager theMemoryBankManager;

int MemoryBankManager::getMemoryRegions(std::vector<memoryRange>& ranges)
{
  std::unique_lock<std::mutex> lock(bankMutex);
  ranges.clear();
  for (unsigned int ix = 0; ix < banks.size(); ix++) {
    memoryRange r;
    r.offset = (size_t)banks[ix].bank->getBaseAddress();
    r.size = (size_t)banks[ix].bank->getSize();
    ranges.push_back(r);
  }
  return 0;
}

void MemoryBankManager::reset()
{
  std::unique_lock<std::mutex> lock(bankMutex);
  // release references to page pools
  pools.clear();
  // release banks
  for (auto& it : banks) {
    int useCount = it.bank.use_count();
    theLog.log(LogInfoDevel_(3008), "Releasing bank %s%s", it.name.c_str(), (useCount == 1) ? "" : "warning - still in use elsewhere !");
  }
  banks.clear();
  poolIndex = -1;
  stopMonitoring();
}

void MemoryBankManager::monitorThLoop() {
  AliceO2::Common::Timer t;
  t.reset(1000000.0 / monitorUpdateRate);
  MemoryPagesPool::Stats mps;

#ifdef WITH_ZMQ
  void* zmqContext = nullptr;
  void* zmqHandle = nullptr;
  bool zmqEnabled = 1;
  void zmqCleanup();
  int zmqError = 0;
  std::string zmqPort = "tcp://127.0.0.1:50002";
  try {
    zmqContext = zmq_ctx_new();
    if (zmqContext == nullptr) {
      zmqError = zmq_errno();
      throw __LINE__;
    }

    zmqHandle = zmq_socket(zmqContext, ZMQ_PUB);
    if (zmqHandle == nullptr) {
      zmqError = zmq_errno();
      throw __LINE__;
    }

    const int cfgZmqLinger = 1000;
    zmqError = zmq_setsockopt(zmqHandle, ZMQ_LINGER, (void*)&cfgZmqLinger, sizeof(cfgZmqLinger)); // close timeout
    if (zmqError) {
      throw __LINE__;
    }

    zmqError = zmq_bind(zmqHandle, zmqPort.c_str());
    if (zmqError) {
      throw __LINE__;
    }

    zmqEnabled = 1;

  } catch (int lineErr) {
    if (zmqError) {
      theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", lineErr, zmqError, zmq_strerror(zmqError));
    } else {
      theLog.log(LogErrorDevel, "Error @%d", lineErr);
    }
    // ZMQ unavailable does not cause consumer to fail starting
    theLog.log(LogErrorDevel, "Memory banks manager: ZMQ stats publishing disabled");
  }
  if (zmqEnabled) {
    theLog.log(LogInfoDevel, "Memory banks manager: ZMQ stats publishing enabled on %s", zmqPort.c_str());
  }
#endif

  for(;!monitorThShutdown.load();) {
    if (t.isTimeout()) {
      std::unique_lock<std::mutex> lock(bankMutex);
      for (auto& it : pools) {
        // it->getId()
        //printf("%s\n", it->getDetailedStats().c_str());
        FILE *fp=fopen(getMonitorFifoPath(it->getId()).c_str(),"w+");
        if (fp!=NULL) {
        //\e[3J
          fprintf(fp,"\ec%s\n\n", it->getDetailedStats().c_str());
          fclose(fp);
        }
      }
#ifdef WITH_ZMQ
      if (zmqEnabled) {
        int msgSize =  0;
        uint32_t numberOfPools = pools.size();
        zmq_send(zmqHandle, &numberOfPools, sizeof(numberOfPools), ZMQ_SNDMORE);
        for (auto& it : pools) {
          it->getDetailedStats(mps);
          zmq_send(zmqHandle, &mps, sizeof(mps), ZMQ_SNDMORE);
          zmq_send(zmqHandle, &mps.states[0], sizeof(mps.states[0]) * mps.states.size(), ZMQ_SNDMORE);
          msgSize += (int)(sizeof(mps) + sizeof(mps.states[0]) * mps.states.size());
        }
        uint32_t trailer = 0xF00F;
        zmq_send(zmqHandle, &trailer, sizeof(trailer), ZMQ_DONTWAIT);
        msgSize += (int)(sizeof(numberOfPools) +  sizeof(trailer));
        //theLog.log(LogDebugDevel, "mem monitor: published %d bytes", msgSize);
      }
#endif
      /*
      for (auto& it : pools) {
        it->getDetailedStats(mps);
        printf("pool %d : %f - %f = %f\n", mps.id, mps.t1, mps.t0, mps.t1-mps.t0);
        for (unsigned int i=0; i<10; i++) {
          if (i>=mps.states.size()) break;
          printf("%d %s %.6f\n",i,MemoryPage::getPageStateString(mps.states[i].state), mps.states[i].timeInCurrentState);
        }
      }
      */

      t.increment();
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(10000));
    }
  }
}


void MemoryBankManager::startMonitoring(double v_updateRate, const char* v_monitorPath) {
  if (monitorTh != nullptr) {
    stopMonitoring();
  }
  if (v_updateRate <= 0) {
    return;
  }
  monitorUpdateRate = v_updateRate;
  if ((v_monitorPath != nullptr) && (strlen(v_monitorPath) != 0)) {
    monitorPath = v_monitorPath;
  } else {
    monitorPath = monitorPathDefault;
  }
  std::function<void(void)> f = std::bind(&MemoryBankManager::monitorThLoop, this);
  monitorThShutdown = 0;
  monitorTh=std::make_unique<std::thread>(f);
}

void MemoryBankManager::stopMonitoring() {
  if (monitorTh != nullptr) {
    monitorThShutdown = 1;
    monitorTh->join();
    monitorTh = nullptr;
  }
}
