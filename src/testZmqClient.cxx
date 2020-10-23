#include <unistd.h>

#include "ZmqClient.hxx"

unsigned long long totalBytes = 0;
unsigned long long intervalBytes = 0;

int callback(void* msg, int msgSize)
{
  //  printf("Block = %d\n",msgSize);
  totalBytes += msgSize;
  intervalBytes += msgSize;
  return 0;
  uint64_t tf;

  if (msgSize == sizeof(tf)) {
    printf("TF %lu\n", *((uint64_t*)msg));
    return 0;
  }
  return -1;
}

int main(int argc, char** argv)
{
  int id = 0;

  if (argc >= 2) {
    id = atoi(argv[1]);
  }

  int nNoData = 999;

  try {
    ZmqClient c("ipc:///tmp/ctp-readout");
    for (;;) {
      c.setCallback(callback);
      sleep(1);
      printf("%d\t%.2f Gb/s\t%.02fMB\n", id, intervalBytes * 8.0 / (1024.0 * 1024.0 * 1024.0), totalBytes / (1024.0 * 1024.0));
      if (intervalBytes == 0) {
        nNoData++;
        if (nNoData == 5) {
          printf("Bytes total = %llu bytes\n", totalBytes);
          break;
        }
      } else {
        nNoData = 0;
      }
      intervalBytes = 0;
    }
  } catch (...) {
  }

  return 0;
}
