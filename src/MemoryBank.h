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

#ifndef _MEMORYBANK_H
#define _MEMORYBANK_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

// a class to handle a big block of contiguous memory.
// constructor/destructor to be overloaded for different types of support.
// destructor should release the associated memory

class MemoryBank
{
 public:
  MemoryBank(std::string description = ""); // constructor

  using ReleaseCallback = std::function<void(void)>;
  MemoryBank(void* baseAddress, std::size_t size, ReleaseCallback callback,
             std::string description); // constructor, given a memory chunk and mean to release it

  virtual ~MemoryBank(); // destructor

  void* getBaseAddress();       // get the (virtual) base address of this memory bank
  std::size_t getSize();        // get the total size (bytes) of this memory bank
  std::string getDescription(); // get the description of this memory bank;

  void clear(); // write zeroes into the whole memory range

 protected:
  void* baseAddress;               // base address (virtual) of buffer
  std::size_t size;                // size of buffer, in bytes
  std::string description;         // description of the memory bank (type/sypport, etc)
  ReleaseCallback releaseCallback; // an optional user-callback to be called in destructor, when overloaded constructor has been used
};

// factory function to create a MemoryBank instance of a given type
// size: size of the bank, in bytes
// support: type of support to be used. Available choices: malloc, MemoryMappedFile
// description: optional description for the memory bank
std::shared_ptr<MemoryBank> getMemoryBank(size_t size, std::string support, std::string description = "");

#endif // #ifndef _MEMORYBANKMANAGER_H

