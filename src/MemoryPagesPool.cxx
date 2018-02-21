#include "MemoryPagesPool.h"

MemoryPagesPool::MemoryPagesPool(size_t vPageSize, size_t vNumberOfPages, void *vBaseAddress, size_t vBaseSize,  ReleaseCallback vCallback) {
  pageSize=vPageSize;
  numberOfPages=vNumberOfPages;
  baseBlockAddress=vBaseAddress;
  baseBlockSize=vBaseSize;
  releaseBaseBlockCallback=vCallback;
  
  size_t sizeNeeded=pageSize * numberOfPages;
  
  if (vBaseSize==0) {
    baseBlockSize=sizeNeeded;
  } else if (sizeNeeded>vBaseSize) {
    numberOfPages=baseBlockSize/pageSize;
  }
  
  pagesAvailable=std::make_unique<AliceO2::Common::Fifo<void *>>(numberOfPages);
  for (size_t i=0; i<numberOfPages; i++) {
    void *ptr=&((char *)baseBlockAddress)[i*pageSize];
    pagesAvailable->push(ptr);
  }
}

MemoryPagesPool::~MemoryPagesPool() {
  if ( (releaseBaseBlockCallback != nullptr) && (baseBlockAddress!=nullptr) ) {
    releaseBaseBlockCallback(baseBlockAddress);
  }
}

void *MemoryPagesPool::getPage() {
  void *ptr=nullptr;
  pagesAvailable->pop(ptr);
  return ptr;
}

void MemoryPagesPool::releasePage(void *address) {
  pagesAvailable->push(address);
}

size_t MemoryPagesPool::getPageSize() {
  return pageSize;
}

size_t MemoryPagesPool::getTotalNumberOfPages() {
  return numberOfPages;
}

size_t MemoryPagesPool::getNumberOfPagesAvailable() {
  return pagesAvailable->getNumberOfUsedSlots();
}

