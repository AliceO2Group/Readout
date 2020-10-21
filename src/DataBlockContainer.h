#ifndef DATAFORMAT_DATABLOCKCONTAINER
#define DATAFORMAT_DATABLOCKCONTAINER

#include <Common/MemPool.h>
#include <functional>
#include <memory>
#include <stdint.h>
#include <stdlib.h>

#include "DataBlock.h"

// A container class for data blocks.
// In particular, allows to take care of the block release after use.

class DataBlockContainer {

public:
  DataBlockContainer(DataBlock *v_data = NULL, uint64_t v_dataBufferSize = 0);
  virtual ~DataBlockContainer();
  DataBlock *getData();
  uint64_t getDataBufferSize();

  using ReleaseCallback = std::function<void(void)>;
  // NB: may use std::bind to add extra arguments

  // this constructor allows to specify a callback which is invoked when container is destroyed
  DataBlockContainer(ReleaseCallback callback, DataBlock *v_data = NULL, uint64_t v_dataBufferSize = 0);

protected:
  DataBlock *data;
  uint64_t dataBufferSize = 0; // Usable memory size pointed by data. Unspecified if zero.
  ReleaseCallback releaseCallback;
};

class DataBlockContainerFromMemPool : public DataBlockContainer {

public:
  DataBlockContainerFromMemPool(std::shared_ptr<MemPool> pool, DataBlock *v_data = NULL);
  ~DataBlockContainerFromMemPool();

private:
  std::shared_ptr<MemPool> mp;
};

/**
 * DataBlockContainer that takes ownership of the payload and deletes it when needed.
 */
class SelfReleasingBlockContainer : public DataBlockContainer {

public:
  SelfReleasingBlockContainer() {
    data = new DataBlock();
    data->data = nullptr;
  }

  ~SelfReleasingBlockContainer() {
    if (data->data != nullptr) {
      delete[] data->data;
    }
    delete data;
  }
};

#endif
