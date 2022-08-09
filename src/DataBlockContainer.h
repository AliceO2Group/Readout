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

#ifndef DATAFORMAT_DATABLOCKCONTAINER
#define DATAFORMAT_DATABLOCKCONTAINER

#include <Common/MemPool.h>
#include <functional>
#include <memory>
#include <stdint.h>
#include <stdlib.h>

//#define DATABLOCKCONTAINER_DEBUG
#ifdef DATABLOCKCONTAINER_DEBUG
#include <stdio.h>
#endif

#include "DataBlock.h"

// A container class for data blocks.
// In particular, allows to take care of the block release after use.

class DataBlockContainer;
using DataBlockContainerReference = std::shared_ptr<DataBlockContainer>;

class DataBlockContainer
{

 public:
  using ReleaseCallback = std::function<void(void)>;
  // NB: may use std::bind to add extra arguments

  // default constructor
  DataBlockContainer(DataBlock* v_data = nullptr, uint64_t v_dataBufferSize = 0) : data(v_data), dataBufferSize(v_dataBufferSize), releaseCallback(nullptr){};

  // this constructor allows to specify a callback which is invoked when container is destroyed
  DataBlockContainer(ReleaseCallback v_callback = nullptr, DataBlock* v_data = nullptr, uint64_t v_dataBufferSize = 0) : data(v_data), dataBufferSize(v_dataBufferSize), releaseCallback(v_callback){};

  // destructor
  virtual ~DataBlockContainer()
  {
    if (releaseCallback != nullptr) {
      releaseCallback();
    }
    #ifdef DATABLOCKCONTAINER_DEBUG
      printf("Releasing DataBlockContainer @ %p\n", data);
    #endif
  };

  DataBlock* getData()
  {
    return data;
  };

  uint64_t getDataBufferSize()
  {
    return dataBufferSize;
  };


  static DataBlockContainerReference getChildBlock(DataBlockContainerReference parentBlock, uint64_t v_dataBufferSizeNeeded, uint64_t roundUp = 0) {
    uint64_t bufferSize = v_dataBufferSizeNeeded + sizeof(DataBlock);
    if (roundUp) {
      uint64_t r = bufferSize % roundUp;
      if (r) {
        bufferSize += roundUp - r;
      }
    }
    if (bufferSize > parentBlock->getData()->header.dataSize - parentBlock->dataBufferUsed) {
      return nullptr;
    }
    DataBlock *b = (DataBlock *)&((char *)parentBlock->getData()->data)[parentBlock->dataBufferUsed];
    b->header = defaultDataBlockHeader;
    b->header.dataSize = v_dataBufferSizeNeeded;
    b->data = &(((char*)b)[sizeof(DataBlock)]);

    #ifdef DATABLOCKCONTAINER_DEBUG
    printf("Block %p - creating child @ %p - payload size %d\n", parentBlock->getData(), b, (int)v_dataBufferSizeNeeded);
    #endif

    DataBlockContainerReference childBlock = std::make_shared<DataBlockContainer>(b, bufferSize);
    if (childBlock == nullptr) {
      return nullptr;
    }
    parentBlock->dataBufferUsed += bufferSize;
    childBlock->parentBlock = parentBlock;
    return childBlock;
  };


 protected:
  DataBlock* data;                 // The DataBlock in use
  uint64_t dataBufferSize = 0;     // Usable memory size pointed by data. Unspecified if zero.
  ReleaseCallback releaseCallback; // Function called on object destroy, to release dataBlock.

  DataBlockContainerReference parentBlock = nullptr; // reference to parent block used, if any
  uint64_t dataBufferUsed = 0; // size of buffer used for child blocks.
};

#endif

