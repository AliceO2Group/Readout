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

/// \file MemoryBanksManager.h
/// \brief Implements a way to keep track of memory banks available at runtime.
/// \descr Some modules create them, some others use them. Allows to allocate
/// pool of pages from the banks.
///
/// \author Sylvain Chapeland (sylvain.chapeland@cern.ch)

#ifndef _MEMORYBANKMANAGER_H
#define _MEMORYBANKMANAGER_H

#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "MemoryBank.h"
#include "MemoryPagesPool.h"

class MemoryBankManager
{

 public:
  MemoryBankManager();  // constructor
  ~MemoryBankManager(); // destructor

  int addBank(std::shared_ptr<MemoryBank> bankPtr,
              std::string name = ""); // add a named memory bank to the manager. By default, takes name from bank description

  // get a pool of pages from the manager, using the banks available
  // parameters:
  // - pageSize: size of one page (in bytes)
  // - pageNumber: number of pages requested
  // - bankName: name of the bank from which to create the pool. If not specified, using the first bank.
  // - firstPageOffset: to control alignment of first page in pool. With zero, start from beginning of big block.
  // - blockAlign: alignment of beginning of big memory block from which pool is created. Pool will start at a multiple of this value.
  // - numaNode: if >= 0, try to allocate the pool on given NUMA node.
  // NB: trivial implementation, once a region from a bank has been used, it can not be reused after the corresponding pool of pages has been release ... don't want to deal with fragmentation etc
  std::shared_ptr<MemoryPagesPool> getPagedPool(size_t pageSize, size_t pageNumber, std::string bankName = "", size_t firstPageOffset = 0, size_t blockAlign = 0, int numaNode = -1);

  // a struct to define a memory range
  struct memoryRange {
    size_t offset; // beginning of memory range (bytes, counted from beginning of block)
    size_t size;   // size of memory range (bytes)
  };

  // a struct to hold bank parameters
  struct bankDescriptor {
    std::string name;                     // bank name
    std::shared_ptr<MemoryBank> bank;     // reference to bank instance
    std::vector<memoryRange> rangesInUse; // list of ranges (with reference to bank base address) currently used in the bank
  };

  // get list of memory regions currently registered
  int getMemoryRegions(std::vector<memoryRange>& ranges);

  // reset bank manager in fresh state, in particular: clear all banks
  void reset();

  void startMonitoring(double updateRate, const char* monitorPath = ""); // starts monitoring at given updateRate and output path
  void stopMonitoring(); // stop monitoring
  std::string getMonitorFifoPath(int id); // return full path to monitor pipe for given pool index

 private:
  std::vector<bankDescriptor> banks; // list of registered memory banks
  std::mutex bankMutex;              // instance mutex to handle concurrent access to public methods
  int poolIndex = -1;                // an increasing index used to assign a unique id to memory pools

  std::unique_ptr<std::thread> monitorTh; // monitor thread
  std::atomic<int> monitorThShutdown; // flag to terminate monitor thread
  void monitorThLoop(); // function launched in monitor thread
  std::vector<std::shared_ptr<MemoryPagesPool>> pools; // reference to existing page pools, for monitoring purpose
  double monitorUpdateRate = 0; // monitoring period, in Hz
  const char *monitorPathDefault = "/tmp/readout-monitor-mempool";
  std::string monitorPath = monitorPathDefault; // prefix path to output monitoring pipes. "-%d" (bank number) added to it and fifo created.
};

// a global MemoryBankManager instance
extern MemoryBankManager theMemoryBankManager;

#endif // #ifndef _MEMORYBANKMANAGER_H

