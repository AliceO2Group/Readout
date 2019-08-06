// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "MemoryHandler.h"

std::unique_ptr<MemoryRegion> bigBlock = nullptr;
std::mutex bigBlockMutex;

#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

MemoryHandler::MemoryHandler(int vPageSize, int vNumberOfPages) {
  pagesAvailable = nullptr;

  pageSize = vPageSize;
  numberOfPages = vNumberOfPages;

  size_t bytesReserved = pageSize * numberOfPages;

  theLog.log("Creating pool of %lu pages of size %lu, total %lu bytes",
             numberOfPages, pageSize, bytesReserved);

  bigBlockMutex.lock();
  size_t bytesFree = bigBlock->size - bigBlock->usedSize;
  if (bytesReserved > bytesFree) {
    bigBlockMutex.unlock();
    theLog.log("No space left in memory bank: available %lu < %lu needed",
               bytesFree, bytesReserved);
    throw __LINE__;
  }
  baseAddress = &(((uint8_t *)bigBlock->ptr)[bigBlock->usedSize]);
  bigBlock->usedSize += bytesReserved;
  bigBlockMutex.unlock();

  //    theLog.log("Got %lld pages, each
  //    %s",nPages,ReadoutUtils::NumberOfBytesToString(pageSize,"Bytes").c_str());
  pagesAvailable = std::make_unique<AliceO2::Common::Fifo<long>>(numberOfPages);

  for (unsigned int i = 0; i < numberOfPages; i++) {
    long offset = i * pageSize;
    // void *page=&((uint8_t*)baseAddress)[offset];
    // printf("%d : 0x%p\n",i,page);
    // theLog.log("Creating page @ offset %d",(int)offset);
    pagesAvailable->push(offset);
  }
  memorySize = bytesReserved;
  theLog.log("%lu pages added, base address=%p size=%lu", numberOfPages,
             baseAddress, memorySize);
}

MemoryHandler::~MemoryHandler() {}

void *MemoryHandler::getPage() {
  long offset = 0;
  int res = pagesAvailable->pop(offset);
  if (res == 0) {
    uint8_t *pagePtr = &baseAddress[offset];
    // theLog.log("%p Using page @ offset %d = %p",this,(int)offset,pagePtr);
    return pagePtr;
  }
  return nullptr;
}

void MemoryHandler::freePage(void *p) {
  int64_t offset = ((uint8_t *)p) - baseAddress;
  // theLog.log("Releasing page @ offset %d (p=%p)",(int)offset,p);
  if ((offset < 0) || (offset > (int64_t)memorySize)) {
    throw __LINE__;
  }
  // theLog.log("Releasing page @ offset %d",(int)offset);
  pagesAvailable->push(offset);
}

void *MemoryHandler::getBaseAddress() { return baseAddress; }
size_t MemoryHandler::getSize() { return memorySize; }
size_t MemoryHandler::getPageSize() { return pageSize; }
