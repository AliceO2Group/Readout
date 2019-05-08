#include "MemoryPagesPool.h"

MemoryPagesPool::MemoryPagesPool(size_t vPageSize, size_t vNumberOfPages, void *vBaseAddress, size_t vBaseSize,  ReleaseCallback vCallback, size_t firstPageOffset) {
  // initialize members from parameters
  pageSize=vPageSize;
  numberOfPages=vNumberOfPages;
  baseBlockAddress=vBaseAddress;
  baseBlockSize=vBaseSize;
  releaseBaseBlockCallback=vCallback;  

  // if not specified, assuming base block size big enough to fit number of pages * page size
  if (baseBlockSize==0) {
    baseBlockSize=pageSize * numberOfPages;
  }
 
  // check validity of parameters
  if ((firstPageOffset>=vBaseSize)||(vBaseSize==0)||(vNumberOfPages==0)||(vPageSize==0)||(baseBlockSize==0)) {
    throw __LINE__;
  }
 
  // if necessary, reduce number of pages to fit in available space
  size_t sizeNeeded=pageSize * numberOfPages + firstPageOffset;
  if (sizeNeeded>baseBlockSize) {    
    numberOfPages=(baseBlockSize-firstPageOffset)/pageSize;
  }
  
  // create a fifo and store list of pages available
  pagesAvailable=std::make_unique<AliceO2::Common::Fifo<void *>>(numberOfPages);
  for (size_t i=0; i<numberOfPages; i++) {
    void *ptr=&((char *)baseBlockAddress)[firstPageOffset+i*pageSize];
    pagesAvailable->push(ptr);
  }
}

MemoryPagesPool::~MemoryPagesPool() {
  // if defined, use provided callback to release base block
  if ( (releaseBaseBlockCallback != nullptr) && (baseBlockAddress!=nullptr) ) {
    releaseBaseBlockCallback(baseBlockAddress);
  }
}

void *MemoryPagesPool::getPage() {
  // get a page from fifo, if available
  void *ptr=nullptr;
  pagesAvailable->pop(ptr);
  return ptr;
}

void MemoryPagesPool::releasePage(void *address) {
  // put back page in list of available pages
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

void *MemoryPagesPool::getBaseBlockAddress() {
  return baseBlockAddress;
}
size_t MemoryPagesPool::getBaseBlockSize() {
  return baseBlockSize;
}

std::shared_ptr<DataBlockContainer> MemoryPagesPool::getNewDataBlockContainer(void *newPage) {
  // get a new page if none provided
  if (newPage==nullptr) {
    // get a new page from the pool
    newPage=getPage();
    if (newPage==nullptr) {
      return nullptr;
    }
  }

  // fill header at beginning of page
  // assuming payload is contiguous after header
  DataBlock *b=(DataBlock *)newPage;
  b->header.blockType=DataBlockType::H_BASE;
  b->header.headerSize=sizeof(DataBlockHeaderBase);
  b->header.dataSize=pageSize-sizeof(DataBlock);
  b->header.id=0;
  b->header.linkId=0;
  b->data=&(((char *)b)[sizeof(DataBlock)]);

  // define a function to put it back in pool after use
  auto releaseCallback = [this, newPage] (void) -> void {
    this->releasePage(newPage);
    return;
  };
  
  // create a container and associate data page and release callback
  std::shared_ptr<DataBlockContainer> bc=std::make_shared<DataBlockContainer>(releaseCallback, (DataBlock*)newPage, pageSize);
  if (bc==nullptr) {
    releaseCallback();
    return nullptr;
  }
  
  //printf("create dbc %p with data=%p stored=%p\n",bc,newPage,bc->getData());
  
  return bc;
}
