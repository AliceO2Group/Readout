// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "MemoryBank.h"
#include <cstring>
#include <memory>
#include <new>

#ifdef WITH_READOUTCARD
#include <ReadoutCard/Exception.h>
#include <ReadoutCard/MemoryMappedFile.h>
#endif
#include <algorithm>
#include <utility>
#include <vector>

#include "readoutInfoLogger.h"

/// generic base class

MemoryBank::MemoryBank(std::string v_description) {
  baseAddress = nullptr;
  size = 0;
  description = v_description;
}

MemoryBank::MemoryBank(void *v_baseAddress, std::size_t v_size,
                       ReleaseCallback v_callback, std::string v_description)
    : baseAddress(v_baseAddress), size(v_size), description(v_description),
      releaseCallback(v_callback) {}

MemoryBank::~MemoryBank() {
  if (releaseCallback != nullptr) {
    releaseCallback();
  }
}

void *MemoryBank::getBaseAddress() { return baseAddress; }

size_t MemoryBank::getSize() { return size; }

std::string MemoryBank::getDescription() { return description; }

void MemoryBank::clear() {
  std::memset(baseAddress, 0, size);
  return;
}

/// MemoryBank implementation with malloc()

class MemoryBankMalloc : public MemoryBank {
public:
  MemoryBankMalloc(size_t size, std::string description);
  ~MemoryBankMalloc();
};

MemoryBankMalloc::MemoryBankMalloc(size_t v_size, std::string v_description)
    : MemoryBank(v_description) {
  baseAddress = malloc(v_size);
  if (baseAddress == nullptr) {
    throw std::bad_alloc();
  }
  size = v_size;
  if (v_description.length() == 0) {
    description = "Bank malloc()";
  }
}

MemoryBankMalloc::~MemoryBankMalloc() {
  if (baseAddress != nullptr) {
    free(baseAddress);
  }
}

#ifdef WITH_READOUTCARD
/// MemoryBank implementation with hugepages
class MemoryBankMemoryMappedFile : public MemoryBank {
public:
  MemoryBankMemoryMappedFile(size_t size, std::string description);
  ~MemoryBankMemoryMappedFile();

private:
  std::unique_ptr<AliceO2::roc::MemoryMappedFile> mMemoryMappedFile;
};

MemoryBankMemoryMappedFile::MemoryBankMemoryMappedFile(
    size_t v_size, std::string v_description)
    : MemoryBank(v_description) {

  // declare available huge page size types and path suffix
  std::vector<std::pair<int, std::string>> hpt = {{1024 * 1024 * 1024, "1GB"},
                                                  {2 * 1024 * 1024, "2MB"}};

  // sort them from biggest to smallest page size
  std::sort(hpt.begin(), hpt.end(),
            [](auto &v1, auto &v2) { return v1.first > v2.first; });

  // select huge page size as big as possible so that target size is a multiple
  // of it
  int hugePageSizeBytes = 0;
  std::string hugePagePath;
  std::string availableSizes;
  const std::string basePath = "/var/lib/hugetlbfs/global/pagesize-";
  for (auto &a : hpt) {
    availableSizes += a.second + " ";
    if ((v_size % a.first == 0)) {
      hugePageSizeBytes = a.first;
      hugePagePath = basePath + a.second;
      break;
    }
  }

  if (hugePageSizeBytes == 0) {
    // no match found
    theLog.log(LogErrorSupport_(3103), "Memory bank %s : selected size %ld must be multiple of "
               "available hugepage sizes = %s",
               v_description.c_str(), v_size, availableSizes.c_str());
    throw __LINE__;
  }

  // path to our memory segment
  std::string memoryMapFilePath = hugePagePath + "/readout-" + v_description;

  // log settings
  theLog.log(LogInfoDevel_(3008), "Creating shared memory block for bank %s : size %ld using %s",
             v_description.c_str(), v_size, memoryMapFilePath.c_str());

  try {
    mMemoryMappedFile = std::make_unique<AliceO2::roc::MemoryMappedFile>(
        memoryMapFilePath, v_size, true); // delete on destruction
  } catch (const AliceO2::roc::MemoryMapException &e) {
    theLog.log(LogErrorSupport_(3230), "Failed to allocate memory buffer : %s", e.what());
    throw __LINE__;
  }

  theLog.log(LogInfoDevel_(3008), "Shared memory block for bank %s is ready", v_description.c_str());
  // todo: check consistent with what requested, alignment, etc
  size = mMemoryMappedFile->getSize();
  baseAddress = (void *)mMemoryMappedFile->getAddress();
  description = v_description;
}

MemoryBankMemoryMappedFile::~MemoryBankMemoryMappedFile() {}
#endif

/// MemoryBank factory based on type
std::shared_ptr<MemoryBank> getMemoryBank(size_t size, std::string type,
                                          std::string description) {

  if (type == "malloc") {
    return std::make_shared<MemoryBankMalloc>(size, description);
  } else if (type == "MemoryMappedFile") {
    #ifdef WITH_READOUTCARD
    return std::make_shared<MemoryBankMemoryMappedFile>(size, description);
    #else
    theLog.log(LogWarningSupport_(3101), "MemoryMappedFile not supported by this build");
    return nullptr;
    #endif
  }
  return nullptr;
}

