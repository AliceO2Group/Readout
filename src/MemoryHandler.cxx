#include "MemoryHandler.h"

std::unique_ptr<MemoryRegion> bigBlock=nullptr;

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;


MemoryHandler::MemoryHandler(int vPageSize, int vNumberOfPages) { 
  pagesAvailable=nullptr;

  memorySize=bigBlock->size;
  baseAddress=(uint8_t *)bigBlock->ptr;

/*
  int baseAlignment=1024*1024;    
  int r=(long)baseAddress % baseAlignment;
  if (r!=0) {
      theLog.log("Unaligned base address %p, target alignment=%d, skipping first %d bytes",baseAddress,baseAlignment,baseAlignment-r);
    int skipBytes=baseAlignment-r;
    baseAddress+=skipBytes;
    memorySize-=skipBytes;
    // now baseAddress is aligned to baseAlignment bytes, but what guarantee do we have that PHYSICAL page addresses are aligned ???
  }
*/

  theLog.log("Creating pool of %d pages of size %d, base address= %p",vNumberOfPages,vPageSize,baseAddress);

  pageSize=vPageSize;
  numberOfPages=vNumberOfPages;
  if (pageSize*numberOfPages>memorySize) {
    theLog.log("Memory bank too small: %ld < %ld * %ld",memorySize,pageSize,numberOfPages);  
    throw __LINE__;
  }
//    theLog.log("Got %lld pages, each %s",nPages,ReadoutUtils::NumberOfBytesToString(pageSize,"Bytes").c_str());
  pagesAvailable=std::make_unique<AliceO2::Common::Fifo<long>>(numberOfPages);

  for (unsigned int i=0;i<numberOfPages;i++) {
    long offset=i*pageSize;
    //void *page=&((uint8_t*)baseAddress)[offset];
    //printf("%d : 0x%p\n",i,page);
    //theLog.log("Creating page @ offset %d",(int)offset);
    pagesAvailable->push(offset);
  }

}

MemoryHandler::~MemoryHandler() {
}

void *MemoryHandler::getPage() {
  long offset=0;
  if (pagesAvailable->pop(offset)==0) {
    uint8_t *pagePtr=&baseAddress[offset];
    //theLog.log("Using page @ offset %d = %p",(int)offset,pagePtr);
    return pagePtr;
  }
  return nullptr;
}

void MemoryHandler::freePage(void *p) {
  int64_t offset=((uint8_t *)p)-baseAddress;
  //theLog.log("Releasing page @ offset %d (p=%p)",(int)offset,p);
  if ((offset<0) || (offset>(int64_t)memorySize)) {
    throw __LINE__;
  }
  //theLog.log("Releasing page @ offset %d",(int)offset);
  pagesAvailable->push(offset);
}
