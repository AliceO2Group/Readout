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

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

class ZmqClient
{
 public:
  ZmqClient(const std::string& url = "tcp://127.0.0.1:50001", const int maxMsgSize = 1024L * 1024L, const int zmqMaxQueue = -1);
  ~ZmqClient();

  int setCallback(std::function<int(void* msg, int msgSize)>);
  void setPause(int); // to pause/unpause data RX

 private:
  std::string cfgAddress;
  int cfgMaxMsgSize;

  void* context = nullptr;
  void* zh = nullptr;
  void* msgBuffer = nullptr;

  bool isPaused = false;

  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run();                       // code executed in separate thread
  std::function<int(void* msg, int msgSize)> callback = nullptr;
};

