#include "MemoryBank.h"
#include <new>
#include <memory>
#include <cstring>

#include <ReadoutCard/MemoryMappedFile.h>
#include <ReadoutCard/Exception.h>
#include <vector>
#include <utility>
#include <algorithm>


#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;


/// generic base class

MemoryBank::MemoryBank(std::string v_description){
  baseAddress=nullptr;
  size=0;
  description=v_description;
}

MemoryBank::MemoryBank(void* v_baseAddress, std::size_t v_size, ReleaseCallback v_callback, std::string v_description)
:baseAddress(v_baseAddress), size(v_size), description(v_description), releaseCallback(v_callback)
{
}

MemoryBank::~MemoryBank(){
  if (releaseCallback!=nullptr) {
    releaseCallback();
  }
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



/// MemoryBank implementation with hugepages
class MemoryBankMemoryMappedFile : public MemoryBank {
  public:
    MemoryBankMemoryMappedFile(size_t size, std::string description);
    ~MemoryBankMemoryMappedFile();
  private:
    std::unique_ptr<AliceO2::roc::MemoryMappedFile> mMemoryMappedFile;
};

MemoryBankMemoryMappedFile::MemoryBankMemoryMappedFile(size_t v_size, std::string v_description) : MemoryBank(v_description) {

    // declare available huge page size types and path suffix
    std::vector<std::pair<int, std::string>> hpt = { {1024*1024*1024, "1GB"}, {2*1024*1024, "2MB"} };

    // sort them from biggest to smallest page size
    std::sort(hpt.begin(),hpt.end(), [](auto &v1, auto &v2) {
      return v1.first > v2.first;
    });
  
    // select huge page size as big as possible so that target size is a multiple of it
    int hugePageSizeBytes=0;
    std::string hugePagePath;
    std::string availableSizes;
    const std::string basePath="/var/lib/hugetlbfs/global/pagesize-";
    for (auto &a: hpt) {
      availableSizes+=a.second + " ";
      if ((v_size % a.first == 0)) {
        hugePageSizeBytes=a.first;
        hugePagePath=basePath + a.second;
        break;
      }
    }

    if (hugePageSizeBytes==0) {
      // no match found
      theLog.log("Memory bank %s : selected size %ld must be multiple of available hugepage sizes = %s",v_description.c_str(),v_size,availableSizes.c_str());
      throw __LINE__;
    }

    // path to our memory segment
    std::string memoryMapFilePath=hugePagePath + "/readout-" + v_description;

    // log settings
    theLog.log("Creating shared memory block for bank %s : size %ld using %s",v_description.c_str(),v_size, memoryMapFilePath.c_str());
    
    try {
      mMemoryMappedFile=std::make_unique<AliceO2::roc::MemoryMappedFile>(memoryMapFilePath,v_size,true); //delete on destruction
    }
    catch (const AliceO2::roc::MemoryMapException& e) {
      theLog.log("Failed to allocate memory buffer : %s",e.what());
      throw __LINE__;
    }
    
    theLog.log("Shared memory block for bank %s is ready",v_description.c_str());
    // todo: check consistent with what requested, alignment, etc
    size=mMemoryMappedFile->getSize();
    baseAddress=(void *)mMemoryMappedFile->getAddress();
    description=v_description;
}

MemoryBankMemoryMappedFile::~MemoryBankMemoryMappedFile() {
}





/// MemoryBank factory based on type
std::shared_ptr<MemoryBank> getMemoryBank(size_t size, std::string type, std::string description) {

  if (type=="malloc") {
    return std::make_shared<MemoryBankMalloc>(size,description);
  } else if (type=="MemoryMappedFile") {
    return std::make_shared<MemoryBankMemoryMappedFile>(size,description);
  }
  return nullptr;
}
