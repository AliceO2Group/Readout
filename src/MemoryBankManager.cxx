#include "MemoryBankManager.h"


MemoryBankManager::MemoryBankManager() {
}


MemoryBankManager::~MemoryBankManager() {
}


int MemoryBankManager::addBank(std::shared_ptr<MemoryBank> bankPtr, std::string name) {

  // disable concurrent execution of this function
  std::unique_lock<std::mutex> lock(bankMutex);

  try {
    if (name.length()==0) {
      name=bankPtr->getDescription();
    }
    banks.push_back({name,bankPtr,{}});
  }
  catch(...) {
    return -1;
  }  
    
  return 0;
}


std::shared_ptr<MemoryPagesPool>  MemoryBankManager::getPagedPool(size_t pageSize, size_t pageNumber, std::string bankName, size_t firstPageOffset, size_t blockAlign){

   void *baseAddress=nullptr; // base address of bank from which the block is taken
   size_t offset=0; // offset of new block (relative to baseAddress)
   size_t blockSize=0; // size of new block (in bytes)

  // disable concurrent execution of this block
  // automatic release of lock when going out of scope  
  // beginning of locked block
  {
    std::unique_lock<std::mutex> lock(bankMutex);

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
      return nullptr;
    }

    // reserve space from big block 
    baseAddress=banks[ix].bank->getBaseAddress();
    offset=0;
    blockSize=pageSize*pageNumber;  // this is the maximum space to use... may loose some pages for alignment

    // alloc new block after existing ranges already in use
    for (auto it=banks[ix].rangesInUse.begin();it!=banks[ix].rangesInUse.end();++it) {
      size_t maxOffset=it->offset+it->size;
      if (maxOffset>offset) {
	offset=maxOffset;
      }
    }

    // align beginning of block as specified
    if (blockAlign>0) {
      size_t bytesExcess=(((size_t)baseAddress)+offset) % blockAlign;
      if (bytesExcess) {
        size_t alignOffset=blockAlign-bytesExcess;
	offset+=alignOffset; // advance to next aligned address
	blockSize-=alignOffset; // decrease block size to respect initial limit
      }
    }
    
    // check not exceeding bank size
    if (offset+blockSize>banks[ix].bank->getSize()) {
      throw std::bad_alloc();
    }

    // keep track of this new block
    banks[ix].rangesInUse.push_back({offset,blockSize});
  }
  // end of locked block
    
  // create pool of pages from new block
  return std::make_shared<MemoryPagesPool>(pageSize,pageNumber,&(((char *)baseAddress)[offset]),blockSize,nullptr,firstPageOffset);
}

// a global MemoryBankManager instance
MemoryBankManager theMemoryBankManager;
