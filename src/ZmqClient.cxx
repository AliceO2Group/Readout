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

#include "ZmqClient.hxx"

#include <functional>
#include <zmq.h>

#include "readoutInfoLogger.h"

ZmqClient::ZmqClient(const std::string& url, int maxMsgSize, int zmqMaxQueue)
{

  cfgAddress = url;
  cfgMaxMsgSize = maxMsgSize;
  msgBuffer = malloc(cfgMaxMsgSize);
  if (msgBuffer == nullptr) {
    throw __LINE__;
  }

  int linerr = 0;
  int zmqerr = 0;
  for (;;) {
    context = zmq_ctx_new();
    if (context == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }
    zh = zmq_socket(context, ZMQ_SUB);
    if (zh == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }
    int timeout = 1000;
    zmqerr = zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*)&timeout, sizeof(int));
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }
    if (zmqMaxQueue >=0 ) {
      zmq_setsockopt(zh, ZMQ_RCVHWM, (void*)&zmqMaxQueue, sizeof(int));
    }
    zmqerr = zmq_connect(zh, cfgAddress.c_str());
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }
    // subscribe to all published messages
    zmqerr = zmq_setsockopt(zh, ZMQ_SUBSCRIBE, "", 0);
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }
    break;
  }

  if ((zmqerr) || (linerr)) {
    theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
    throw __LINE__;
  }

  // starting snapshot thread
  shutdownRequest = 0;
  std::function<void(void)> l = std::bind(&ZmqClient::run, this);
  th = std::make_unique<std::thread>(l);
}

ZmqClient::~ZmqClient()
{
  shutdownRequest = 1;
  if (th != nullptr) {
    th->join();
    th = nullptr;
  }
  if (zh != nullptr) {
    zmq_close(zh);
    zh = nullptr;
  }
  if (context != nullptr) {
    zmq_ctx_destroy(context);
    context = nullptr;
  }
  if (msgBuffer != nullptr) {
    free(msgBuffer);
    msgBuffer = nullptr;
  }
}

/*
int ZmqClient::publish(void *msgBody, int msgSize){
  return 0;
}
*/

int ZmqClient::setCallback(std::function<int(void* msg, int msgSize)> cb)
{
  callback = cb;
  return 0;
}

void ZmqClient::setPause(int pause)
{
  isPaused = pause;
}

void ZmqClient::run()
{
  #ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), "zmq-client");
  #endif
  for (; !shutdownRequest;) {
    int linerr = 0, zmqerr = 0;
    for (;;) {
      if (isPaused) {
        usleep(10000);
        continue;
      }

      int nb = 0;
      nb = zmq_recv(zh, msgBuffer, cfgMaxMsgSize, 0);
      if (nb >= cfgMaxMsgSize) {
        // buffer was too small to gt full message
        theLog.log(LogWarningDevel, "ZMQ message bigger than buffer, skipping");
        break;
      }

      if ((callback != nullptr) && (nb > 0) && (!isPaused)) {
        if (callback(msgBuffer, nb)) {
          linerr = __LINE__;
          break;
        }
      }
      break;

      uint64_t tf;
      if (nb == sizeof(tf)) {
        printf("TF %lu\n", (unsigned long)*((uint64_t*)msgBuffer));
      }
      break;
      if (nb == 0) {
        linerr = __LINE__;
        break;
      }
      ((char*)msgBuffer)[nb] = 0;
      printf("recv %d = %s\n", nb, (char*)msgBuffer);
      break;
    }
    if ((zmqerr) || (linerr)) {
      printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    }
  }
  return;
}

