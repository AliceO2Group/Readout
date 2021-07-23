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

///
/// @file    TtyChecker.h
/// @author  Sylvain
///

#pragma once

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

class TtyChecker
{
 private:
  bool isInteractive = false;
  struct termios initial_settings;

 public:
  TtyChecker()
  {
    // if launched from terminal, force logs to terminal
    if (isatty(fileno(stdin))) {
      if (getenv("O2_INFOLOGGER_MODE") == nullptr) {
        //printf("Launching from terminal, logging here\n");
        setenv("O2_INFOLOGGER_MODE", "stdout", 1);
      }
      isInteractive = true;
    }
    if (isInteractive) {
      // set non-blocking input
      fcntl(0, F_SETFL, O_NONBLOCK);
      struct termios new_settings;
      tcgetattr(0, &initial_settings);
      new_settings = initial_settings;
      new_settings.c_lflag &= ~ICANON;
      new_settings.c_lflag &= ~ECHO;
      // do not disable ctrl+c signal
      // new_settings.c_lflag &= ~ISIG;
      // The following controls buffer timeouts, but seem not ok on CS8
//      new_settings.c_cc[VMIN] = 0;
//      new_settings.c_cc[VTIME] = 0;
      tcsetattr(0, TCSANOW, &new_settings);
    }
  };
  ~TtyChecker()
  {
    if (isInteractive) {
      // restore term settings
      tcsetattr(0, TCSANOW, &initial_settings);
    }
  };
};

