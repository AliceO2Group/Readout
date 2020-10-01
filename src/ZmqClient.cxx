#include "ZmqClient.hxx"
#include <zmq.h>
#include <functional>

ZmqClient::ZmqClient(const std::string &url){

  cfgAddress = url;
  
  int linerr=0;
  int zmqerr=0;
  for (;;) {
    context = zmq_ctx_new ();
    if (context==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    zh = zmq_socket (context, ZMQ_SUB);
    if (zh==nullptr) { linerr=__LINE__; zmqerr=zmq_errno(); break; }
    int timeout = 1000;
    zmqerr=zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*) &timeout, sizeof(int));
    if (zmqerr) { linerr=__LINE__; break; }
    zmqerr=zmq_connect(zh,cfgAddress.c_str());
    if (zmqerr) { linerr=__LINE__; break; }
    // subscribe to all published messages
    zmqerr=zmq_setsockopt( zh, ZMQ_SUBSCRIBE, "", 0 );
    if (zmqerr) { linerr=__LINE__; break; }
    break;
  }

  if ((zmqerr)||(linerr)) {
    printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    throw __LINE__;
  }

  // starting snapshot thread
  shutdownRequest = 0;
  std::function<void(void)> l = std::bind(&ZmqClient::run, this);
  th = std::make_unique<std::thread>(l);
}

ZmqClient::~ZmqClient(){
  shutdownRequest = 1;
  if (th != nullptr) {
    th->join();
    th = nullptr;
  }
}

/*
int ZmqClient::publish(void *msgBody, int msgSize){
  return 0;
}
*/

int ZmqClient::setCallback(std::function<int(void *msg, int msgSize)> cb) {
  callback = cb;
  return 0;
}

void ZmqClient::run() { 
  for (;!shutdownRequest;) {
    int linerr=0, zmqerr=0;
    for (;;) {
      char buffer [128];
      int nb=0;
      nb=zmq_recv (zh, buffer, sizeof(buffer), 0);
      
      if ((callback!=nullptr)&&(nb>0)) {
        if (callback(buffer, nb)) {
	  linerr=__LINE__; break;
	}
      }
      break;
      
      uint64_t tf;
      if (nb==sizeof(tf)) {
        printf("TF %lu\n",*((uint64_t *)buffer));
      }
      break;
      if (nb==0) { linerr=__LINE__; break; }
      buffer[nb]=0;
      printf("recv %d = %s\n", nb, buffer);
      break;
    }
    if ((zmqerr)||(linerr)) {
      printf("ZeroMQ error @%d : (%d) %s\n", linerr, zmqerr, zmq_strerror(zmqerr));
    }
  }  
  return;
}
