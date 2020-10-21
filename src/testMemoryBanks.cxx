// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

// simple test program to exercise the classes related to memory banks

#include <memory>

#include "MemoryBank.h"
#include "MemoryBankManager.h"

int main() {
  MemoryBankManager bm;

  size_t poolPages = 100;
  size_t pageSize = 1024 * 1024;
  size_t size = poolPages * pageSize;
  int nBanks = 4;

  for (int j = 0; j < nBanks; j++) {
    std::shared_ptr<MemoryBank> b = nullptr;
    try {
      b = getMemoryBank(size, "malloc", "malloc:" + std::to_string(j));
    } catch (...) {
    }
    if (b == nullptr) {
      printf("Failed to create bank[%d]\n", j);
      continue;
    }
    printf("Create [%d]=%s\n", j, b->getDescription().c_str());
    char *ptr = (char *)b->getBaseAddress();
    for (size_t i = 0; i < b->getSize(); i++) {
      ptr[i] = i % 100;
    }
    bm.addBank(b);
  }

  for (int j = 0; j < 6; j++) {
    std::shared_ptr<MemoryPagesPool> p;
    try {
      p = bm.getPagedPool(pageSize, poolPages / 5, "malloc:0");
    } catch (...) {
    }
    if (p == nullptr) {
      printf("Pool %d : failed to alloc\n", j);
      continue;
    }
    printf("Pool %d : %d pages available\n", j, (int)p->getNumberOfPagesAvailable());
  }

  int nTestPages = 5;
  std::shared_ptr<MemoryPagesPool> thePool;
  try {
    thePool = bm.getPagedPool(pageSize, nTestPages, "malloc:1");
  } catch (...) {
  }
  if (thePool == nullptr) {
    printf("Failed to create test pool\n");
  } else {
    printf("test pool %d pages available\n", (int)thePool->getNumberOfPagesAvailable());
  }
  std::vector<void *> thePages;
  for (int i = 0; i <= nTestPages; i++) {
    void *newPage = thePool->getPage();
    if (newPage != nullptr) {
      printf("Got page #%d = %p, %d/%d available\n", i, newPage, (int)thePool->getNumberOfPagesAvailable(), (int)thePool->getTotalNumberOfPages());
      thePages.push_back(newPage);
    } else {
      printf("Failed to get page #%d\n", i);
    }
  }
  printf("releasing pages\n");
  for (auto p : thePages) {
    thePool->releasePage(p);
    printf("Pool: %d/%d available\n", (int)thePool->getNumberOfPagesAvailable(), (int)thePool->getTotalNumberOfPages());
  }
  for (int i = 0; i <= nTestPages; i++) {
    void *newPage = thePool->getPage();
    if (newPage != nullptr) {
      printf("Got page #%d = %p\n", i, newPage);
      printf("Got page #%d = %p, %d/%d available\n", i, newPage, (int)thePool->getNumberOfPagesAvailable(), (int)thePool->getTotalNumberOfPages());
      thePages.push_back(newPage);
    } else {
      printf("Failed to get page #%d\n", i);
    }
  }

  return 0;
}
