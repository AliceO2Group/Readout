#include "ZmqServer.hxx"

#include <functional>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

void ZmqServer::init() {
  int linerr = 0;
  int zmqerr = 0;
  for (;;) {
    context = zmq_ctx_new();
    if (context == nullptr) {
      linerr = __LINE__;
      zmqerr = zmq_errno();
      break;
    }
    zh = zmq_socket(context, ZMQ_PUB);
    /*
    if (zh==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    int timeout = 1000;
    zmqerr=zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, sizeof(int));
    if (zmqerr) { linerr=__LINE__; break; }
    int linger = 0;
    zmqerr=zmq_setsockopt(zh, ZMQ_LINGER, (void*) &linger, sizeof(int));
    if (zmqerr) { linerr=__LINE__; break; }
    */
    zmqerr = zmq_bind(zh, cfgAddress.c_str());
    if (zmqerr) {
      linerr = __LINE__;
      break;
    }
    break;
  }

  if ((zmqerr) || (linerr)) {
    printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    throw __LINE__;
  } else {
    printf("ZeroMQ server started @ %s\n", cfgAddress.c_str());
  }

  // starting snapshot thread
  shutdownRequest = 0;
  std::function<void(void)> l = std::bind(&ZmqServer::run, this);
  th = std::make_unique<std::thread>(l);
}

ZmqServer::~ZmqServer() {
  shutdownRequest = 1;
  if (th != nullptr) {
    th->join();
    th = nullptr;
  }
}

int ZmqServer::publish(void *msgBody, int msgSize) {
  int err = zmq_send(zh, msgBody, msgSize, 0);
  // printf("publish %d bytes = %d\n",msgSize,err);
  return err;
}

void ZmqServer::run() {
  return;
  uint64_t i = 0;
  for (; !shutdownRequest;) {
    int linerr = 0, zmqerr = 0;
    i++;
    publish(&i, sizeof(i));
    for (;;) {
      break;
      char buf[128];
      snprintf(buf, 128, "Hello %d", (int)i);
      zmqerr = publish(buf, sizeof(buf));
      // zmqerr=zmq_send (zh, buf, strlen(buf), 0);
      // zmqerr=zmq_send(zh, "World", 5, 0);
      // if (zmqerr) { linerr=__LINE__; break; }
      printf("PUB: %s = %d\n", buf, zmqerr);
      break;
    }
    if ((zmqerr) || (linerr)) {
      printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    }
    sleep(1);
  }
  return;
}
