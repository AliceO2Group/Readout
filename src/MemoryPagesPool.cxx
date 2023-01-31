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

#include "MemoryPagesPool.h"

#include <cassert>
#include <cstdio>

#include "ReadoutUtils.h"
#define ENABLE_LOG_CODEWRONG
#include "readoutInfoLogger.h"


int MemoryPagesPoolStatsEnabled = 0; // flag to control memory stats

MemoryPagesPool::MemoryPagesPool(size_t vPageSize, size_t vNumberOfPages, void* vBaseAddress, size_t vBaseSize, ReleaseCallback vCallback, size_t firstPageOffset, int vId)
{
  // initialize members from parameters
  pageSize = vPageSize;
  numberOfPages = vNumberOfPages;
  baseBlockAddress = vBaseAddress;
  baseBlockSize = vBaseSize;
  releaseBaseBlockCallback = vCallback;
  state = BufferState::empty;
  id = vId;

  // check page / header sizes
  if (headerReservedSpace) {
    assert(headerReservedSpace >= sizeof(DataBlock));
    assert(headerReservedSpace <= pageSize);
  }

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

  // create metadata
  pages.resize(numberOfPages);
  for (auto &p : pages) {
    p.resetPageStates();
    p.setPageState(MemoryPage::PageState::Idle);
  }

  // create a fifo and store list of pages available
  pagesAvailable = std::make_unique<AliceO2::Common::Fifo<void*>>(numberOfPages);
  void* ptr = nullptr;
  int id = 0;
  for (size_t i = 0; i < numberOfPages; i++) {
    ptr = &((char*)baseBlockAddress)[firstPageOffset + i * pageSize];
    if (i == 0) {
      firstPageAddress = ptr;
    }
    pages[i].pagePtr = ptr;
    pages[i].pageSize = pageSize;
    pages[i].pageId = id;
    // check that indexing works as expected
    // disable validity range check as first/last pages not defined yet.
    if ( (int)i != id ) {
      throw __LINE__;
    }
    if ( (int)i != getPageIndexFromPagePtr(ptr,0)) {
      throw __LINE__;
    }
    pagesAvailable->push(ptr);
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

  // udpate buffer state
  updateBufferState();
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
  // disable concurrent execution of this function
  std::unique_lock<std::mutex> lock(pagesAvailableMutexPop);

  // update statistics
  poolStats.set((CounterValue)getNumberOfPagesAvailable());

  // get a page from fifo, if available
  void* ptr = nullptr;
  pagesAvailable->pop(ptr);

  updatePageState(ptr, MemoryPage::PageState::Allocated);

  // udpate buffer state
  updateBufferState();

  // the following does not need exclusive access, release lock
  lock.unlock();
  
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

  updatePageState(address, MemoryPage::PageState::Idle);

  // disable concurrent execution of this function
  std::unique_lock<std::mutex> lock(pagesAvailableMutexPush);

  // put back page in list of available pages
  pagesAvailable->push(address);

  // udpate buffer state
  updateBufferState();
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

  int ix = getPageIndexFromPagePtr(newPage);
  if (ix < 0) {
    throw __LINE__;
  }

  // fill header at beginning of page assuming payload is contiguous after header
  // or in a separate area, depending on space reserved at top of page
  DataBlock* b = nullptr;

  if (headerReservedSpace) {
    // previous implementation: keep space at beginning of page for headers
    b = (DataBlock*)newPage;
    b->data = &(((char*)b)[headerReservedSpace]);
  } else {
    // metadata and payload are separated
    b = pages[ix].getDataBlockPtr();
    b->data = (char*)pages[ix].getPagePtr();
  }

  // printf("block = %p header = %p data =%p   reserved = %d offset: %d\n", b, &b->header, b->data, (int)headerReservedSpace, (int)(b->data - (char *)&b->header));

  b->header = defaultDataBlockHeader;
  b->header.dataSize = getDataBlockMaxSize();
  b->header.memorySize = getPageSize();

  // define a function to put it back in pool after use
  auto releaseCallback = [this, newPage](void) -> void {
    this->releasePage(newPage);
    return;
  };

  // create a container and associate data page and release callback
  std::shared_ptr<DataBlockContainer> bc = std::make_shared<DataBlockContainer>(releaseCallback, b, getPageSize());
  if (bc == nullptr) {
    releaseCallback();
    return nullptr;
  } else {
    bc->memoryPagesPoolPtr = this;
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

std::string MemoryPagesPool::getStats() {
  return "number of pages used: " + std::to_string(poolStats.getTotal()) + " average free pages: " + std::to_string((uint64_t)poolStats.getAverage()) + " minimum free pages: " + std::to_string(poolStats.getMinimum());
}


void MemoryPagesPool::setWarningCallback(const LogCallback& cb, double vthHigh, double vthOk) {
  thHigh = vthHigh;
  thOk = vthOk;
  theLogCallback = cb;
}

void MemoryPagesPool::log(const std::string &msg) {
  if (theLogCallback!=nullptr) {
    // the log callback will format it with appropriate text headers (eg who owns the pool)
    theLogCallback(msg);
  }
}

void MemoryPagesPool::updateBufferState() {
  if (theLogCallback == nullptr) {
    return;
  }
  double r = 1.0 - (getNumberOfPagesAvailable() * 1.0 / getTotalNumberOfPages());
  if ((r == 1.0) && (state != BufferState::full)) {
    state = BufferState::full;
    log("buffer full");
  } else if ((r > thHigh) && (state == BufferState::empty)) {
    state = BufferState::high;
    log("buffer usage is high");
  } else if ((r < thOk) && ((state == BufferState::full) || (state == BufferState::high))) {
    state = BufferState::empty;
    log("buffer usage back to reasonable level");
  }
  if (pBufferState != nullptr) {
    pBufferState->store(r);
  }
}

int MemoryPagesPool::getId() {
  return id;
}

void MemoryPagesPool::setBufferStateVariable(std::atomic<double> *bufferStateVar) {
  pBufferState=bufferStateVar;
  updateBufferState();
  //printf("buffer usage = %lf @ 0x%p\n", pBufferState->load(),pBufferState);
}

int MemoryPagesPool::getNumaStats(std::map<int,int> &pagesCountPerNumaNode) {
  int err=0;
  pagesCountPerNumaNode.clear();
  uint64_t np = 0;
  for(char *ptr = (char *)baseBlockAddress; ptr < &((char*)baseBlockAddress)[baseBlockSize]; ptr += 4096) {
    int numaNode = -1;
    if (numaGetNodeFromAddress(ptr, numaNode) == 0) {
      if (numaNode>=0) {
        np++;
	pagesCountPerNumaNode[numaNode]++;
      }
    }
  }
  for (auto &c : pagesCountPerNumaNode) {
    c.second = c.second / 250; // report in MB
  }

/*  for (auto& p : pagesMap) {
    int numaNode = -1;
    if (numaGetNodeFromAddress(p.second.ptr, numaNode) == 0) {
      if (numaNode>=0) {
        pagesCountPerNumaNode[numaNode]++;
      } else {
        err++;
      }
    } else {
      err++;
    }
  }
*/
  return err;
}

int MemoryPagesPool::getPageIndexFromPagePtr(void *ptr, int checkValidity) {
  if (ptr == nullptr) return -1;
  if (checkValidity) {
    if (ptr<firstPageAddress) return -1;
    if (ptr>(char *)lastPageAddress) return -1;
  }
  int ix = (int)(((char *)ptr - (char *)firstPageAddress) / pageSize);
  if ((ix < 0) || (ix >= (int)pages.size())) return -1;
  if (pages[ix].pagePtr != ptr) return -1;
  return ix;
}

int MemoryPagesPool::updatePageState(void *ptr, MemoryPage::PageState state) {
  int ix = getPageIndexFromPagePtr(ptr);
  if (ix<0) return -1;
  if (ix<0) {
    theLog.log("Page %d.%d %p going from %d (%s) to %d (%s)", getId(), ix, ptr, (int)pages[ix].currentPageState,MemoryPage::getPageStateString(pages[ix].currentPageState),(int)state,MemoryPage::getPageStateString(state));
  }
  pages[ix].setPageState(state);
  return 0;
}

MemoryPage::MemoryPage() {
  resetPageStates();
  pagePtr = nullptr;
  pageSize = 0;
  dataBlock.header = defaultDataBlockHeader;
  dataBlock.data = nullptr;
  pageId = -1;
}

MemoryPage::~MemoryPage() {
  //reportPageStates();
}

void MemoryPage::setPageState(PageState s) {
  if (s != currentPageState) {
    if (currentPageState != PageState::Undefined) {
      if (pageStateTimes[(int)currentPageState].t0IsValid) {
        pageStateTimes[(int)currentPageState].duration +=
          (std::chrono::duration<double>(std::chrono::steady_clock::now() - pageStateTimes[(int)currentPageState].t0)).count();
      }
      pageStateTimes[(int)currentPageState].t0IsValid = 0;
    }
    if (s != PageState::Undefined) {
      pageStateTimes[(int)s].t0 = std::chrono::steady_clock::now();
      pageStateTimes[(int)s].t0IsValid = 1;
    }
    currentPageState = s;
  }
}

void MemoryPage::resetPageStates() {
  currentPageState = PageState::Undefined;
  for (int i=0; i<(int)PageState::Undefined; i++) {
    pageStateTimes[i].t0IsValid = 0;
    pageStateTimes[i].duration = 0;
  }
  nTimeUsed = 0;
}

double MemoryPage::getPageStateDuration(PageState s) {
  if (s != PageState::Undefined) {
    return pageStateTimes[(int)s].duration;
  }
  return 0;
}


const char* MemoryPage::getPageStateString(PageState s) {
  switch (s) {
    case Idle: return "Idle";
    case Allocated: return "Allocated";
    case InROC: return "InROC";
    case InEquipment: return "InEquipment";
    case InEquipmentFifoOut: return "InEquipmentFifoOut";
    case InAggregator: return "InAggregator";
    case InAggregatorFifoOut: return "InAggregatorFifoOut";
    case InConsumer: return "InConsumer";
    case InFMQ: return "InFMQ";
    case Undefined: return "Undefined";
  }
  return "Unknown";
}

int updatePageStateFromDataBlockContainerReference(DataBlockContainerReference b, MemoryPage::PageState state) {
  int err = __LINE__;
  MemoryPagesPool *mp = nullptr;
  DataBlock *db = nullptr;
  void *pagePtr = nullptr;
  for (;;) {
    if (b == nullptr) {
      err = __LINE__;
      break;
    }
    mp = ((MemoryPagesPool *)b->memoryPagesPoolPtr);
    if (mp == nullptr) {
      err = __LINE__;
      break;
    }
    db = b->getData();
    if (db == nullptr) {
      err = __LINE__;
      break;
    }
    pagePtr = db->data;
    err = mp->updatePageState(pagePtr, state);
    break;
  }
  if (err) {LOG_CODEWRONG;}
  return err;
}

void MemoryPage::reportPageStates() {
  double t = 0;
  for (int i=0; i<PageState::Undefined; i++) {
    t += getPageStateDuration((PageState)i);
  }

  printf("Page %p : ", getPagePtr());
  if (t != 0) {
    printf ("%12.6fs\t",t);
    for (int i=0; i<PageState::Undefined; i++) {
      //printf("%s = %.2f%%   ", getPageStateString((PageState)i), getPageStateDuration((PageState)i) * 100 / t);
      printf("%.2f%% \t", getPageStateDuration((PageState)i) * 100 / t);
    }
  }
  printf("\n");
}
