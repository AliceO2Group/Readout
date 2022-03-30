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

// readoutAdminDB
// A command line utility to admin readout database

#include <Common/Configuration.h>

#include "ReadoutConst.h"
#include "ReadoutDatabase.h"

#include <stdio.h>
#include <InfoLogger/InfoLogger.hxx>
using namespace AliceO2::InfoLogger;



void printUsage()
{
  printf("Usage: readoutAdminDB ...\n");
  printf("  -c command : action to execute. One of create (create tables), clear (delete tables content), destroy (destroy all tables), fetch (retrieve content), test (insert some dummy data)\n");  
  printf("  [-z pathToConfigurationFile] : sets which configuration to use. By default %s\n", cfgDefaultsPath.c_str());
  printf("  [-v] : sets verbose mode\n");
  printf("  [-h] : print this help\n");
}

int main(int argc, char* argv[])
{

  ConfigFile config;                               // handle to configuration
  std::string configPath(cfgDefaultsPath.c_str()); // path to configuration
  std::string command;                             // command to be executed

  bool optCreate = 0;       // create main message table
  bool optClear = 0;        // delete content of main message table
  bool optDestroy = 0;      // destroy all message tables
  bool optNone = 0;         // no command specified - test DB access only
  bool optTest = 0;         // test some insert/update (dummy data)
  bool optFetch = 0;        // dump DB content
  bool optStatus = 0;       // dump DB status
  bool optVerbose = 0;      // verbose mode
  setenv("O2_INFOLOGGER_MODE", "stdout", 1);
  InfoLogger theLog;

  theLog.log("readoutAdminDB");

  // parse command line parameters
  int option;
  while ((option = getopt(argc, argv, "z:c:vh")) != -1) {

    switch (option) {

      case 'z':
        configPath = optarg;
        break;

      case 'c':
        command = optarg;
        break;

      case 'v':
        optVerbose = 1;
	break;
	
      case 'h':
      case '?':
        printUsage();
        return 0;
        break;

      default:
        theLog.log("Invalid command line argument %c", option);
        return -1;
    }
  }

  if (command == "create") {
    optCreate = 1;
  } else if (command == "clear") {
    optClear = 1;
  } else if (command == "destroy") {
    optDestroy = 1;
  } else if (command == "test") {
    optTest = 1;
  } else if (command == "fetch") {
    optFetch = 1;
  } else if (command == "status") {
    optStatus = 1;
  } else if (command == "") {
    optNone = 1;
  } else {
    theLog.log("Unkown command %s", command.c_str());
    return -1;
  }

  // load readout configuration file
  try {
    config.load(configPath);
  } catch (std::string err) {
    theLog.log("Failed to load configuration: %s", err.c_str());
    return -1;
  }

  // load configuration file
  std::string dbCx;
  config.getOptionalValue("readout.db", dbCx);

  if ((optCreate)||(optClear)||(optDestroy)||(optNone)) {
  }
   
  try {
    
    ReadoutDatabase l(dbCx.c_str());
    l.verbose = optVerbose;
    
    int err = 0;
    theLog.log("DB connected");
    
    if ((!err)&&(optCreate)) {
      err = l.createTables();
    }
    if ((!err)&&(optClear)) {
      err = l.clearTables();
    }
    if ((!err)&&(optDestroy)) {
      err = l.destroyTables();
    }
    if ((!err)&&(optFetch)) {
      err = l.dumpTablesContent();
    }
    if ((!err)&&(optStatus)) {
      err = l.dumpTablesStatus();
    }    
    if (err) {
      theLog.log("%s", l.getError());
    } else {
      theLog.log("success");
    }
    
    if (optTest) {
      theLog.log("Updating");
      for(int j=1; j< 100; j++) {
        std::string occRole = "flp-test-"+std::to_string(j);
        unsigned int occRunNumber = 1;
        l.initRunCounters(occRole.c_str(), occRunNumber);
	for (int i=0; i<3; i++) {      
	  l.updateRunCounters(i,i,i,i);
	  //usleep(100000);
	}
      }
      theLog.log("Done updating");
    }
  }
  catch (int err) {
    printf("Database failed: %d\n",err);
  }
  return 0;	


  return 0;
}

