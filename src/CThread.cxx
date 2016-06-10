#include "CThread.h"
#include <stdlib.h>
#include <unistd.h>

using namespace AliceO2::InfoLogger;

CThread::CThread(CThread::CallbackResult (*vLoopCallback)(void *), void *vLoopArg, std::string vThreadName, int vLoopSleepTime) {
  shutdown=0;
  running=0;
  theThread=NULL;
  name=vThreadName;
  loopCallback=vLoopCallback;
  loopArg=vLoopArg;
  loopSleepTime=vLoopSleepTime;
}

CThread::~CThread() {
  if (theThread!=NULL) {
    stop();
    join();
  }
}

void CThread::start() {
  if (theThread==NULL) {
    shutdown=0;
    running=0;
    theThread= new std::thread(threadMain,this);
  }
  return;  
}

void CThread::stop() {
  if (theThread!=NULL) {
    shutdown=1;
  }
}

void CThread::join() {
  if (theThread!=NULL) {
    shutdown=1;
    theThread->join();
    delete theThread;
    theThread=NULL;
  }
  return;  
}

void CThread::threadMain(CThread *e) {
  e->theLog.log("Thread %s starting",e->name.c_str());
  e->running=1;
  int maxIterOnShutdown=100;
  int nIterOnShutdown=0;
  
  for(;;) {
//    e->theLog.log("Thread %s loop",e->name.c_str());
    if (e->shutdown) {
      if (nIterOnShutdown>=maxIterOnShutdown) break;
      nIterOnShutdown++;
    }
    int r=e->doLoop();
    if (r==CThread::CallbackResult::Ok) {
    } else if (r==CThread::CallbackResult::Idle) {
      if (e->shutdown) break; // exit immediately on shutdown
      usleep(e->loopSleepTime);
    } else if (r==CThread::CallbackResult::Error) {
      // account this error... maybe do something if repetitive
      e->theLog.log("thread [%s] -> doLoop error",e->name.c_str());
      if (e->shutdown) break; // exit immediately on shutdown
    } else {
      e->theLog.log("thread [%s] -> doLoop undefined return code",e->name.c_str());
      break;
    }
  }
  e->running=0;
  e->theLog.log("Thread %s completed",e->name.c_str());
}


CThread::CallbackResult CThread::doLoop() {
//  theLog.log("doLoop %s -- %p, %p, %p",this->name.c_str(),this,loopCallback, loopArg);
  if (loopCallback!=NULL) {
    return loopCallback(loopArg);
  }
  return CThread::CallbackResult::Idle;
}


std::string CThread::getName() {
  return name;
}

