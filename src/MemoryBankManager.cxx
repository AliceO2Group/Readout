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

#include "readoutInfoLogger.h"

MemoryBankManager::MemoryBankManager() {}

MemoryBankManager::~MemoryBankManager() {}

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
  bzero(blockAddress, blockSize);
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
  return std::make_shared<MemoryPagesPool>(pageSize, pageNumber, &(((char*)baseAddress)[offset]), blockSize, nullptr, firstPageOffset, newId);
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
  for (auto& it : banks) {
    int useCount = it.bank.use_count();
    theLog.log(LogInfoDevel_(3008), "Releasing bank %s%s", it.name.c_str(), (useCount == 1) ? "" : "warning - still in use elsewhere !");
  }
  banks.clear();
  poolIndex = -1;
}

