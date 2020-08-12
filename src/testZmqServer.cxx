#include "ZmqServer.hxx"
#include <unistd.h>

int main() {
  try {
    ZmqServer s;
    for(;;) {
      sleep(1);
    }
  }
  catch(...) {
  }
  
  return 0;
}
