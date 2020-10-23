#include <unistd.h>

#include "ZmqServer.hxx"

int main()
{
  try {
    ZmqServer s("tcp://127.0.0.1:50001");
    for (;;) {
      sleep(1);
    }
  } catch (...) {
  }

  return 0;
}
