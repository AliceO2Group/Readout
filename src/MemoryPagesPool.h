#ifndef _MEMORYPAGESPOOL_H
#define _MEMORYPAGESPOOL_H

#include <memory>
#include <functional>
#include <Common/Fifo.h>
#include <Common/DataBlockContainer.h>


// optimized for 1-1 consumers (1 thread to get the page, 1 thread to release them)
// no check on address of data pages pushed back
// base address should be kept while object in use


class MemoryPagesPool {

public:

  using ReleaseCallback = std::function<void(void*)>;
  // NB: may use std::bind to add extra arguments

 // constructor (args: size of each page and number of pages in the pool, base address where to create the pages)
  MemoryPagesPool(size_t pageSize, size_t numberOfPages, void *baseAddress, size_t baseSize=0, ReleaseCallback callback=nullptr);
  ~MemoryPagesPool(); // destructor

  void *getPage(); // get a new page from the pool (if available, nullptr if none)
  void releasePage(void *address); // insert back page to the pool after use, to make it available again

  size_t getPageSize(); // retrieve the page size
  size_t getTotalNumberOfPages(); // retrieve number of pages in pool
  size_t getNumberOfPagesAvailable(); //retrieve number of pages currently available

  std::shared_ptr<DataBlockContainer>getNewDataBlockContainer(); // returns an empty data block container (with data = a new page) from the pool. Page will be put back in pool after use.

private:
  std::unique_ptr<AliceO2::Common::Fifo<void *>> pagesAvailable;  // a buffer to keep track of individual pages
  
  size_t numberOfPages; // number of pages
  size_t pageSize; // size of each page, in bytes

  void * baseBlockAddress; // address of block containing all pages
  size_t baseBlockSize; // size of block containing all pages
  
  ReleaseCallback releaseBaseBlockCallback;
};


#endif // #ifndef _MEMORYPAGESPOOL_H
