#include "MemoryBankManager.h"


MemoryBankManager::MemoryBankManager() {
}


MemoryBankManager::~MemoryBankManager() {
}


int MemoryBankManager::addBank(std::shared_ptr<MemoryBank> bankPtr, std::string name) {

  // disable concurrent execution of this function
  bankMutex.lock();
  
  try {
    if (name.length()==0) {
      name=bankPtr->getDescription();
    }
    banks.push_back({name,bankPtr,{}});
  }
  catch(...) {
    bankMutex.unlock();
    return -1;
  }  
    
  bankMutex.unlock();
  return 0;
}


std::shared_ptr<MemoryPagesPool>  MemoryBankManager::getPagedPool(size_t pageSize, size_t pageNumber, std::string bankName){

  // disable concurrent execution of this function
  bankMutex.lock();
  
  // look for corresponding named bank
  // if not specified, used first one...
  unsigned int ix=0;
  bool bankFound=false;
  if (bankName.size()>0) {
    for (ix=0;ix<banks.size();ix++) {
      if (banks[ix].name==bankName) {
        bankFound=true;
        break;
      }
    }
  } else {
    if (banks.size()) {
      ix=0;
      bankFound=true;
    }
  }
  if (!bankFound) {
    bankMutex.unlock();
    return nullptr;
  }

  // reserve space from big block 
  size_t offset=0; // offset of new block
  void *baseAddress=banks[ix].bank->getBaseAddress(); // base address of bank
  size_t blockSize=pageSize*pageNumber; // size of new block

  // alloc new block after existing ranges already in use
  for (auto it=banks[ix].rangesInUse.begin();it!=banks[ix].rangesInUse.end();++it) {
    size_t maxOffset=it->offset+it->size;
    if (maxOffset>offset) {
      offset=maxOffset;
    }
  }

  // check not exceeding bank size
  if (offset+blockSize>banks[ix].bank->getSize()) {
    bankMutex.unlock();
    throw std::bad_alloc();
  }
  
  // keep track of this new block
  banks[ix].rangesInUse.push_back({offset,blockSize});
  bankMutex.unlock();
  
  // create pool of pages from it
  return std::make_shared<MemoryPagesPool>(pageSize,pageNumber,&(((char *)baseAddress)[offset]),blockSize);
}

MemoryBankManager theMemoryBankManager;
