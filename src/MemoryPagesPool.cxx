// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "MemoryPagesPool.h"

#include <cassert>
#include <cstdio>

int MemoryPagesPoolStatsEnabled = 0; // flag to control memory stats

MemoryPagesPool::MemoryPagesPool(size_t vPageSize, size_t vNumberOfPages, void* vBaseAddress, size_t vBaseSize, ReleaseCallback vCallback, size_t firstPageOffset)
{
  // initialize members from parameters
  pageSize = vPageSize;
  numberOfPages = vNumberOfPages;
  baseBlockAddress = vBaseAddress;
  baseBlockSize = vBaseSize;
  releaseBaseBlockCallback = vCallback;

  // check page / header sizes
  assert(headerReservedSpace >= sizeof(DataBlock));
  assert(headerReservedSpace <= pageSize);

  // if not specified, assuming base block size big enough to fit number of pages * page size
  if (baseBlockSize == 0) {
    baseBlockSize = pageSize * numberOfPages;
  }

  // check validity of parameters
  if ((firstPageOffset >= vBaseSize) || (vBaseSize == 0) || (vNumberOfPages == 0) || (vPageSize == 0) || (baseBlockSize == 0)) {
    throw __LINE__;
  }

  // if necessary, reduce number of pages to fit in available space
  size_t sizeNeeded = pageSize * numberOfPages + firstPageOffset;
  if (sizeNeeded > baseBlockSize) {
    numberOfPages = (baseBlockSize - firstPageOffset) / pageSize;
  }

  // create a fifo and store list of pages available
  pagesAvailable = std::make_unique<AliceO2::Common::Fifo<void*>>(numberOfPages);
  void* ptr = nullptr;
  int id = 0;
  for (size_t i = 0; i < numberOfPages; i++) {
    ptr = &((char*)baseBlockAddress)[firstPageOffset + i * pageSize];
    pagesAvailable->push(ptr);
    if (i == 0) {
      firstPageAddress = ptr;
    }
    if (MemoryPagesPoolStatsEnabled) {
      DataPageDescriptor d;
      d.id = id;
      d.ptr = ptr;
      d.timeGetPage = 0.0;
      d.timeGetDataBlock = 0.0;
      d.timeReleasePage = 0.0;
      d.nTimeUsed = 0;
      pagesMap[ptr] = d;
    }
    id++;
  }
  lastPageAddress = ptr;

  if (MemoryPagesPoolStatsEnabled) {
    // enable histograms for t1..t4
    t1.enableHistogram(64, 1, 100000000);
    t2.enableHistogram(64, 1, 100000000);
    t3.enableHistogram(64, 1, 100000000);
    t4.enableHistogram(64, 1, 100000000);
  }
}

MemoryPagesPool::~MemoryPagesPool()
{
  if (MemoryPagesPoolStatsEnabled) {
    printf("memory pool statistics: \n");
    printf("getpage->getdatablock");
    printf(": avg=%.0lf  min=%llu  max=%llu  count=%llu \n", t1.getAverage(), (unsigned long long)t1.getMinimum(), (unsigned long long)t1.getMaximum(), (unsigned long long)t1.getCount());
    printf("getdatablock->releasepage");
    printf(": avg=%.0lf  min=%llu  max=%llu  count=%llu \n", t2.getAverage(), (unsigned long long)t2.getMinimum(), (unsigned long long)t2.getMaximum(), (unsigned long long)t2.getCount());
    printf("releasepage->getpage");
    printf(": avg=%.0lf  min=%llu  max=%llu  count=%llu \n", t3.getAverage(), (unsigned long long)t3.getMinimum(), (unsigned long long)t3.getMaximum(), (unsigned long long)t3.getCount());
    printf("getpage->releasepage");
    printf(": avg=%.0lf  min=%llu  max=%llu  count=%llu \n", t4.getAverage(), (unsigned long long)t4.getMinimum(), (unsigned long long)t4.getMaximum(), (unsigned long long)t4.getCount());

    std::vector<double> tx;
    std::vector<CounterValue> tv1, tv2, tv3, tv4;
    t1.getHisto(tx, tv1);
    t2.getHisto(tx, tv2);
    t3.getHisto(tx, tv3);
    t4.getHisto(tx, tv4);

    CounterValue ts1 = 0, ts2 = 0, ts3 = 0, ts4 = 0;
    for (unsigned int i = 0; i < tx.size(); i++) {
      ts1 += tv1[i];
      ts2 += tv2[i];
      ts3 += tv3[i];
      ts4 += tv4[i];
    }
    for (unsigned int i = 0; i < tx.size(); i++) {
      double t = tx[i] / 1000000.0;
      double tr1 = 0.0, tr2 = 0.0, tr3 = 0.0, tr4 = 0.0;
      if (ts1 != 0) {
        tr1 = tv1[i] * 100.0 / ts1;
      }
      if (ts2 != 0) {
        tr2 = tv2[i] * 100.0 / ts2;
      }
      if (ts3 != 0) {
        tr3 = tv3[i] * 100.0 / ts3;
      }
      if (ts4 != 0) {
        tr4 = tv4[i] * 100.0 / ts4;
      }
      printf("%.1e   \t%.2lf\t%.2lf\t%.2lf\t%.2lf\n", t, tr1, tr2, tr3, tr4);
    }

    int nNeverUsed = 0;
    for (auto& p : pagesMap) {
      // printf("page id %d used %d\n",p.second.id,p.second.nTimeUsed);
      if (p.second.nTimeUsed <= 10) {
        nNeverUsed++;
      }
    }
    printf("Pages never used: %d\n", nNeverUsed);
  }

  // if defined, use provided callback to release base block
  if ((releaseBaseBlockCallback != nullptr) && (baseBlockAddress != nullptr)) {
    releaseBaseBlockCallback(baseBlockAddress);
  }
}

void* MemoryPagesPool::getPage()
{
  // get a page from fifo, if available
  void* ptr = nullptr;
  pagesAvailable->pop(ptr);

  // stats
  if (MemoryPagesPoolStatsEnabled) {
    if (ptr != nullptr) {
      auto search = pagesMap.find(ptr);
      if (search != pagesMap.end()) {
        search->second.timeGetPage = clock.getTime();
        if (search->second.timeReleasePage > 0) {
          t3.set((uint64_t)((search->second.timeGetPage - search->second.timeReleasePage) * 1000000));
        }
        search->second.timeGetDataBlock = 0;
        search->second.timeReleasePage = 0;
        search->second.nTimeUsed++;
      }
    }
  }

  return ptr;
}

void MemoryPagesPool::releasePage(void* address)
{
  // safety check on address provided
  if (!isPageValid(address)) {
    throw __LINE__;
  }

  // stats
  if (MemoryPagesPoolStatsEnabled) {
    auto search = pagesMap.find(address);
    if (search != pagesMap.end()) {
      search->second.timeReleasePage = clock.getTime();
      if (search->second.timeGetDataBlock > 0) {
        t2.set((uint64_t)((search->second.timeReleasePage - search->second.timeGetDataBlock) * 1000000));
      }
      if (search->second.timeGetPage > 0) {
        t4.set((uint64_t)((search->second.timeReleasePage - search->second.timeGetPage) * 1000000));
      }
    }
  }

  // put back page in list of available pages
  pagesAvailable->push(address);
}

size_t MemoryPagesPool::getPageSize() { return pageSize; }
size_t MemoryPagesPool::getTotalNumberOfPages() { return numberOfPages; }
size_t MemoryPagesPool::getNumberOfPagesAvailable() { return pagesAvailable->getNumberOfUsedSlots(); }
void* MemoryPagesPool::getBaseBlockAddress() { return baseBlockAddress; }
size_t MemoryPagesPool::getBaseBlockSize() { return baseBlockSize; }

std::shared_ptr<DataBlockContainer> MemoryPagesPool::getNewDataBlockContainer(void* newPage)
{
  // get a new page if none provided
  if (newPage == nullptr) {
    // get a new page from the pool
    newPage = getPage();
    if (newPage == nullptr) {
      return nullptr;
    }
  } else {
    // safety check on address provided
    if (!isPageValid(newPage)) {
      throw __LINE__;
    }
  }

  // stats
  if (MemoryPagesPoolStatsEnabled) {
    auto search = pagesMap.find(newPage);
    if (search != pagesMap.end()) {
      search->second.timeGetDataBlock = clock.getTime();
      if (search->second.timeGetPage > 0) {
        t1.set((uint64_t)((search->second.timeGetDataBlock - search->second.timeGetPage) * 1000000));
      }
    }
  }

  // fill header at beginning of page assuming payload is contiguous after header
  DataBlock* b = (DataBlock*)newPage;
  b->header = defaultDataBlockHeader;
  b->header.dataSize = getDataBlockMaxSize();
  b->data = &(((char*)b)[headerReservedSpace]);

  // define a function to put it back in pool after use
  auto releaseCallback = [this, newPage](void) -> void {
    this->releasePage(newPage);
    return;
  };

  // create a container and associate data page and release callback
  std::shared_ptr<DataBlockContainer> bc = std::make_shared<DataBlockContainer>(releaseCallback, (DataBlock*)newPage, pageSize);
  if (bc == nullptr) {
    releaseCallback();
    return nullptr;
  }

  // printf("create dbc %p with data=%p stored=%p\n",bc,newPage,bc->getData());

  return bc;
}

bool MemoryPagesPool::isPageValid(void* pagePtr)
{
  if (pagePtr < firstPageAddress) {
    return false;
  }
  if (pagePtr > lastPageAddress) {
    return false;
  }
  if ((((char*)pagePtr - (char*)firstPageAddress) % pageSize) != 0) {
    return false;
  }
  return true;
}

size_t MemoryPagesPool::getDataBlockMaxSize() { return pageSize - headerReservedSpace; }
