#include "MemoryBank.h"
#include <new>
#include <memory>
#include <cstring>

/// generic base class

MemoryBank::MemoryBank(std::string v_description){
  baseAddress=nullptr;
  size=0;
  description=v_description;
}

MemoryBank::~MemoryBank(){
}

void *MemoryBank::getBaseAddress() {
  return baseAddress;
}

size_t MemoryBank::getSize() {
  return size;
}

std::string MemoryBank::getDescription() {
  return description;
}

void MemoryBank::clear() {
  std::memset(baseAddress,0,size);
  return;
}


/// MemoryBank implementation with malloc()

class MemoryBankMalloc : public MemoryBank {
  public:
    MemoryBankMalloc(size_t size, std::string description);
    ~MemoryBankMalloc();
};

MemoryBankMalloc::MemoryBankMalloc(size_t v_size, std::string v_description) : MemoryBank(v_description) {
  baseAddress=malloc(v_size);
  if (baseAddress==nullptr) {
    throw std::bad_alloc();
  }
  size=v_size;
  if (v_description.length()==0) {
    description="Bank malloc()";
  }
}

MemoryBankMalloc::~MemoryBankMalloc() {
  if (baseAddress!=nullptr) {
    free(baseAddress);
  }
}

std::shared_ptr<MemoryBank> getMemoryBank(size_t size, std::string type, std::string description) {

  if (type=="malloc") {
    return std::make_shared<MemoryBankMalloc>(size,description);
  }
  return nullptr;
}
