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


#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
using namespace AliceO2::InfoLogger;

//#include "ZmqClient.hxx"
//#include "TtyChecker.h"
#include <zmq.h>
#include "MemoryPagesPool.h"

#ifdef WITH_SDL
#include <SDL2/SDL.h>
//#include <SDL2/SDL_ttf.h>
#endif

// set log environment before theLog is initialized
// use console output, non-blocking input
//TtyChecker theTtyChecker;

// log handle
InfoLogger theLog;

#include <unistd.h>
#include <inttypes.h>

int callback(void* msg, int msgSize)
{
  (void)msg;
  printf("Block = %d\n",msgSize);
  return 0;
}

int main(int argc, char** argv)
{
  theLog.setContext(InfoLoggerContext({ { InfoLoggerContext::FieldName::Facility, (std::string) "readout/memview" } }));
  
  std::string port = "tcp://127.0.0.1:50002"; // ZMQ server address
  int pageSize = 1024L * 1024L;               // ZMQ RX buffer size, should be big enough to receive a full report
  int maxQueue = 1;                           // ZMQ input queue size
  
  // parse options
  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    std::string key(option);
    size_t separatorPosition = key.find('=');
    if (separatorPosition == std::string::npos) {
      theLog.log(LogErrorOps, "Failed to parse option '%s'\n", option);
      continue;
    }
    key.resize(separatorPosition);
    std::string value = &(option[separatorPosition + 1]);
    if (key == "port") {
      port = value;
    }
    if (key == "pageSize") {
      pageSize = atoi(value.c_str());
    }
    if (key == "maxQueue") {
      maxQueue = atoi(value.c_str());
    }
  }
  
  void* context = nullptr;
  void* zh = nullptr;
  std::vector<char *> msgBuffer;
  std::vector<unsigned int> msgSize;
      
  const int maxBlocks = 32; // maximum number of message parts
    
  try {

    msgBuffer.resize(maxBlocks+1);
    msgSize.resize(maxBlocks+1);
    for (int i=0; i <= maxBlocks; i++) {
      auto ptr = (char *)malloc(pageSize);
      if (ptr == nullptr) {
        theLog.log(LogErrorDevel, "Failed to allocate %d x %d buffer", maxBlocks, pageSize);
        throw __LINE__;
      }
      msgBuffer[i] = ptr;
    }

    int linerr = 0;
    int zmqerr = 0;
    for (;;) {
      context = zmq_ctx_new();
      if (context == nullptr) {
        linerr = __LINE__;
        zmqerr = zmq_errno();
        break;
      }
      zh = zmq_socket(context, ZMQ_SUB);
      if (zh == nullptr) {
        linerr = __LINE__;
        zmqerr = zmq_errno();
        break;
      }
      int timeout = 1000;
      zmqerr = zmq_setsockopt(zh, ZMQ_RCVTIMEO, (void*)&timeout, sizeof(int));
      if (zmqerr) {
        linerr = __LINE__;
        break;
      }
      if (maxQueue >=0 ) {
        zmq_setsockopt(zh, ZMQ_RCVHWM, (void*)&maxQueue, sizeof(int));
      }
      zmqerr = zmq_connect(zh, port.c_str());
      if (zmqerr) {
        linerr = __LINE__;
        break;
      }
      // subscribe to all published messages
      zmqerr = zmq_setsockopt(zh, ZMQ_SUBSCRIBE, "", 0);
      if (zmqerr) {
        linerr = __LINE__;
        break;
      }
      break;
    }

    if ((zmqerr) || (linerr)) {
      theLog.log(LogErrorDevel, "ZeroMQ error @%d : (%d) %s", linerr, zmqerr, zmq_strerror(zmqerr));
      throw __LINE__;
    }

  }
  catch (...) {
    theLog.log(LogErrorDevel, "Failed to initialize client");
    return -1;
  }
  
  #ifdef WITH_SDL
    SDL_Window* hWnd = NULL;
    SDL_Renderer* hRenderer = NULL;
    SDL_Event event;

    int szx = 1920;
    int szy = 1080;

    SDL_Init(SDL_INIT_VIDEO);
    hWnd = SDL_CreateWindow("FLP memory", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, szx, szy, SDL_WINDOW_SHOWN); // | SDL_WINDOW_RESIZABLE);
    if (!hWnd) return -1;
    hRenderer = SDL_CreateRenderer(hWnd, -1, 0);
    if ((!hWnd) || (!hRenderer)) {
        return -1;
    }
    SDL_SetRenderDrawColor(hRenderer, 0, 0, 0, 0);
    SDL_RenderClear(hRenderer);
    SDL_RenderPresent(hRenderer);
    
    //TTF_Font* Sans = TTF_OpenFont("Sans.ttf", 24);
  #endif
    
  auto doRx = [&]() {
    unsigned int bufIx;
    unsigned int rxBytes = 0;
    for (unsigned int i = 0; ;i++) {
      bufIx = i < maxBlocks ? i : maxBlocks;
      int nb = 0;
      nb = zmq_recv(zh, msgBuffer[bufIx], pageSize, 0);
      if (nb >= pageSize) {
        // buffer was too small to gt full message
        theLog.log(LogWarningDevel, "ZMQ message bigger than buffer, skipping");
        break;
      }
      if (nb<0) break;
      rxBytes += nb;
      msgSize[bufIx]=nb;
      //printf("Got %d = %d\n",bufIx, nb);
      int more;
      size_t size = sizeof(int);
      if (zmq_getsockopt(zh, ZMQ_RCVMORE, &more, &size) != 0) break;
      if (!more) break;
    }
    if (rxBytes) {
      bool isOk = 0;
      if ((bufIx>=2) && (bufIx<maxBlocks) && (msgSize[0] == 4) && (msgSize[bufIx]==4)) {
        uint32_t nPools = *((uint32_t *)msgBuffer[0]);
        uint32_t trailer = *((uint32_t *)msgBuffer[bufIx]);
        if ((trailer == 0xF00F)&&((nPools * 2 + 1) == bufIx)) {
          //printf("%d pools\n", (int)nPools);                 
          isOk = 1;

          for (unsigned int p=0; p<nPools; p++) {
            if (msgSize[1+p*2]!=sizeof(MemoryPagesPool::Stats)) {
              isOk = 0;
              break;
            }
            auto stats = ((MemoryPagesPool::Stats *)msgBuffer[1+p*2]);
            unsigned int npages = msgSize[2+p*2] / sizeof(MemoryPagesPool::PageStat);
            //printf("%d %d: %f - %f - %u\n",p, stats->id, stats->t0,stats->t1,npages);
            auto ps = (MemoryPagesPool::PageStat*)msgBuffer[2+p*2];
            int c = 0;
            for (unsigned int k = 0; k<npages; k++) {
              if (ps[k].state != MemoryPage::PageState::Idle) c++;
            }
            //printf("busy pages = %d / %u\n",c,npages);
          }

          if (isOk) {                    
          #ifdef WITH_SDL
          int szx, szy;
          //SDL_RenderSetViewport(hRenderer, NULL);
          SDL_GetRendererOutputSize(hRenderer, &szx, &szy);
	  SDL_SetRenderDrawColor(hRenderer, 0, 0, 0, 0);
          SDL_RenderClear(hRenderer);
          
          //printf("%d,%d\n", szx,szy);

          int border=10;
          // one column per pool
          int cw=(szx-(nPools+1)*border)/nPools;
          int cy=szy-2*border;
          //printf("cw,cy= %d, %d\n",cw, cy);
          for (unsigned int p=0; p<nPools; p++) {
            int ox=border+(border+cw)*p;
            int oy=border;
            //printf("ox,oy= %d, %d\n",ox, oy);
            SDL_SetRenderDrawColor(hRenderer, 0, 0, 255, 255);
            SDL_Rect r = {ox,oy,cw,cy};
            SDL_RenderDrawRect(hRenderer, &r);
            
            auto stats = ((MemoryPagesPool::Stats *)msgBuffer[1+p*2]);
            unsigned int npages = msgSize[2+p*2] / sizeof(MemoryPagesPool::PageStat);
            auto ps = (MemoryPagesPool::PageStat*)msgBuffer[2+p*2];

            unsigned int bb = 2; // internal border
            unsigned int sq=(cw-2*bb)*(cy-2*bb);
            unsigned int pxk = (int)sqrt(sq/npages);
            pxk=6;
            //printf("pxk=%d\n",pxk);
            unsigned int npl = (cw-bb) / pxk;
            for (unsigned int k = 0; k<npages; k++) {
              switch (ps[k].state) {
              case MemoryPage::PageState::Idle:
                SDL_SetRenderDrawColor(hRenderer, 48, 48, 48, 255);
                break;
              case MemoryPage::PageState::InROC:
                SDL_SetRenderDrawColor(hRenderer, 0, 255, 255, 255);
                break;
              case MemoryPage::PageState::InFMQ:
                SDL_SetRenderDrawColor(hRenderer, 255, 128, 128, 255);
                break;
              case MemoryPage::PageState::InAggregator:
                SDL_SetRenderDrawColor(hRenderer, 255, 255, 0, 255);
                break;
              default:
                SDL_SetRenderDrawColor(hRenderer, 200,200,200, 255);              
                break;
              }
              int rx = k % npl;
              int ry = k / npl;
              SDL_Rect r = {ox+bb+rx*pxk+1,oy+bb+ry*pxk+1,pxk-2,pxk-2};
              SDL_RenderFillRect(hRenderer, &r);
            }
          }
          
          SDL_RenderPresent(hRenderer);          
          #endif
          }
          
        }
      }
      if (!isOk) {
        printf("Wrong message received\n");
      }
    }
  };
  
  /*
  for(;;) {
    doRx();
  }   
  return 0;
  */ 
  
  #ifdef WITH_SDL

    int shutdown = 0;
    while (!shutdown) {


        if (SDL_PollEvent(&event)) {
            //printf("event type=%d\n",(int)event.type);
            shutdown=1;
            switch (event.type) {
            case SDL_QUIT:
                printf("exiting\n");
                shutdown = 1;
                break;
                
            case SDL_KEYDOWN:
             int key = event.key.keysym.sym;
             if (key == SDLK_ESCAPE) {
               shutdown = 1;
             }
            break;
            }
        }
        else {
          doRx();
          SDL_Delay(10);
        }
    }
    SDL_DestroyRenderer(hRenderer);
    SDL_DestroyWindow(hWnd);
    SDL_Quit();
  exit(0);
  #endif // WITH_SDL

  for(;;) {
    doRx();
  }   
  
  return 0;
}

