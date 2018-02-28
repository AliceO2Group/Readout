/// \file MemoryBanksManager.h
/// \brief Implements a way to keep track of memory banks available at runtime.
/// \descr Some modules create them, some others use them. Allows to allocate pool of pages from the banks.
///
/// \author Sylvain Chapeland (sylvain.chapeland@cern.ch)

#ifndef _MEMORYBANKMANAGER_H
#define _MEMORYBANKMANAGER_H

#include "MemoryBank.h"
#include "MemoryPagesPool.h"
#include <map>
#include <memory>
#include <mutex>

class MemoryBankManager {
  
  public:
  
  MemoryBankManager(); // constructor
  ~MemoryBankManager(); // destructor
  
  int addBank(std::shared_ptr<MemoryBank> bankPtr, std::string name=""); // add a named memory bank to the manager. By default, takes name from bank description
  std::shared_ptr<MemoryPagesPool> getPagedPool(size_t pageSize, size_t pageNumber, std::string bankName=""); // get a pool of pages from the manager
  
  // a struct to define a memory range
  struct memoryRange {
    size_t offset; // beginning of memory range (bytes)
    size_t size; // size of memory range (bytes)
  };
  
  // a struct to hold bank parameters
  struct bankDescriptor {
    std::string name; // bank name
    std::shared_ptr<MemoryBank> bank; // reference to bank instance
    std::vector<memoryRange> rangesInUse; // list of ranges (with reference to bank base address) currently used in the bank
  };
  
  private:
 
  std::vector<bankDescriptor> banks; // list of registered memory banks
  std::mutex bankMutex; // instance mutex to handle concurrent access to public methods
};

extern MemoryBankManager theMemoryBankManager;

#endif // #ifndef _MEMORYBANKMANAGER_H
