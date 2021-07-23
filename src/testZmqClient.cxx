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

#include <unistd.h>
#include <inttypes.h>

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
    printf("TF %" PRIu64 "\n", *((uint64_t*)msg));
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

