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
#include <memory>
#include <string>
#include <thread>

class ZmqServer
{
 public:
  ZmqServer() { init(); };
  ZmqServer(const std::string& url)
  {
    cfgAddress = url;
    init();
  };

  ~ZmqServer();
  int publish(void* msgBody, int msgSize);

 private:
  void init();

  std::string cfgAddress = "tcp://127.0.0.1:50001";

  void* context = nullptr;
  void* zh = nullptr;

  std::unique_ptr<std::thread> th;  // thread receiving data
  std::atomic<int> shutdownRequest; // flag to be set to 1 to stop thread
  void run();                       // code executed in separate thread
};

