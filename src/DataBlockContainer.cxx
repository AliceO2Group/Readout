#include "DataBlockContainer.h"

#include <string>

// base DataBlockContainer class

DataBlockContainer::DataBlockContainer(DataBlock *v_data, uint64_t v_dataBufferSize) : data(v_data), dataBufferSize(v_dataBufferSize), releaseCallback(nullptr) {}

DataBlockContainer::DataBlockContainer(ReleaseCallback v_callback, DataBlock *v_data, uint64_t v_dataBufferSize) : data(v_data), dataBufferSize(v_dataBufferSize), releaseCallback(v_callback) {}

DataBlockContainer::~DataBlockContainer() {
  if (releaseCallback != nullptr) {
    releaseCallback();
  }
}

DataBlock *DataBlockContainer::getData() { return data; }

uint64_t DataBlockContainer::getDataBufferSize() { return dataBufferSize; }

// container for data pages coming fom MemPool class

DataBlockContainerFromMemPool::DataBlockContainerFromMemPool(std::shared_ptr<MemPool> pool, DataBlock *v_data) {
  mp = pool;
  if (mp == nullptr) {
    throw std::string("NULL argument");
  }
  data = v_data;
  if (data == NULL) {
    data = (DataBlock *)mp->getPage();
    if (data == NULL) {
      throw std::string("No page available");
    }
  }
}

DataBlockContainerFromMemPool::~DataBlockContainerFromMemPool() {
  if (mp != nullptr) {
    if (data != nullptr) {
      mp->releasePage(data);
    }
  }
}
