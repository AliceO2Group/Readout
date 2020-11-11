#include "ZmqClient.hxx"

#include <functional>
#include <zmq.h>

#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
using namespace AliceO2::InfoLogger;
extern InfoLogger theLog;

ZmqClient::ZmqClient(const std::string& url, int maxMsgSize)
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
        printf("TF %lu\n", *((uint64_t*)msgBuffer));
      }
      break;
      if (nb == 0) {
        linerr = __LINE__;
        break;
      }
      ((char*)msgBuffer)[nb] = 0;
      printf("recv %d = %s\n", nb, msgBuffer);
      break;
    }
    if ((zmqerr) || (linerr)) {
      printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    }
  }
  return;
}
