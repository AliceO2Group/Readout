#include "ZmqServer.hxx"
#include <unistd.h>

int main() {
  try {
    ZmqServer s("tcp://127.0.0.1:50001");
    for(;;) {
      sleep(1);
    }
  }
  catch(...) {
  }
  
  return 0;
}
