#include "ZmqClient.hxx"
#include <unistd.h>

int callback(void *msg, int msgSize) {
  uint64_t tf;
  if (msgSize==sizeof(tf)) {
    printf("TF %lu\n",*((uint64_t *)msg));
    return 0;
  }
  return -1;
}

int main() {
  try {
    ZmqClient c;
    c.setCallback(callback);
    
    sleep(5);
  }
  catch(...) {
  }
  
  return 0;
}
