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

// Program reading on stdin and dispatching copy of input to all connected clients

#include "SocketRx.h"
#include <stdio.h>

int main() {
  try {
    SocketRx s("testDispatch", 10001);

    while (1) {
      char buf[1024];
      char* r = fgets(buf, sizeof(buf), stdin);
      (void)r; // Suppress compiler warning
      if (feof(stdin)) {
	break;
      }
      s.broadcast(buf);
    }
  }
  catch(...) {
    return -1;
  }
  
  return 0;
}
