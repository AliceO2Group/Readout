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

#include <Common/Fifo.h>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>

#include "DataBlockContainer.h"
#include "ReadoutUtils.h"

struct MemoryRegion {
  unsigned long long size; // size of memory block
  void* ptr;               // pointer to memory block
  std::string name;        // name of the region

  // todo: add callback to release region afterwards

  unsigned long long usedSize; // amount of memory in use
};

extern std::unique_ptr<MemoryRegion> bigBlock;

// todo: named list of big blocks, with get/set functions

// todo: big blocks: one different per equipment to avoid lock

// a big block of memory for I/O

class MemoryHandler
{

 public:
  MemoryHandler(int pageSize, int numberOfPages);
  ~MemoryHandler();

  void* getPage();
  void freePage(void*);

  void* getBaseAddress();
  size_t getSize();
  size_t getPageSize();

 private:
  size_t memorySize;                                           // total size of buffer
  size_t pageSize;                                             // size of each superpage in buffer (not the one of getpagesize())
  size_t numberOfPages;                                        // number of pages allocated
  uint8_t* baseAddress;                                        // base address of buffer
  std::unique_ptr<AliceO2::Common::Fifo<long>> pagesAvailable; // a buffer to keep track of individual pages. storing offset (with respect to base address) of pages available
};

class DataBlockContainerFromMemoryHandler : public DataBlockContainer
{
 private:
  std::shared_ptr<MemoryHandler> mMemoryHandler;

 public:
  DataBlockContainerFromMemoryHandler(std::shared_ptr<MemoryHandler> const& h) : DataBlockContainer((DataBlock*)nullptr)
  {

    mMemoryHandler = h;

    data = nullptr;
    try {
      data = new DataBlock;
    } catch (...) {
      throw __LINE__;
    }
    data->data = (char*)h->getPage();
    if (data->data == nullptr) {
      delete data;
      throw __LINE__;
    }
    // printf("In use: page %p\n",data->data);
  }

  ~DataBlockContainerFromMemoryHandler()
  {
    if (data != nullptr) {
      // printf("~DataBlockContainerFromMemoryHandler(%p) : header=%p data=%p\n",this,data,data->data);

      if (data->data != nullptr) {
        mMemoryHandler->freePage(data->data);
      }

      delete data;
    }
  }
};

