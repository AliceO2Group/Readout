
#include "SocketRx.h"

#include <functional>
#include <unistd.h>

#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <string>

#define BUFSZ 64*1024

#include <math.h>

#include <InfoLogger/InfoLoggerMacros.hxx>
using namespace AliceO2::InfoLogger;

// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x) / sizeof(x[0])

std::string NumberOfBytesToString(double value, const char* suffix, int base = 1000)
{
  const char* prefixes[] = { "", "k", "M", "G", "T", "P" };
  int maxPrefixIndex = STATIC_ARRAY_ELEMENT_COUNT(prefixes) - 1;
  int prefixIndex = log(value) / log(base);
  if (prefixIndex > maxPrefixIndex) {
    prefixIndex = maxPrefixIndex;
  }
  if (prefixIndex < 0) {
    prefixIndex = 0;
  }
  double scaledValue = value / pow(base, prefixIndex);
  char bufStr[64];
  if (suffix == nullptr) {
    suffix = "";
  }
  snprintf(bufStr, sizeof(bufStr) - 1, "%.03lf %s%s", scaledValue, prefixes[prefixIndex], suffix);
  return std::string(bufStr);
}


    
void SocketRx::closeClient(SocketRxClient &c) {
  close(c.fd);  

  double t0=c.t.getTime();
  double rateRx=(c.bytesRx/t0);
  double rateTx=(c.bytesTx/t0);

  if (theLog != nullptr) {
  theLog->log(LogInfoDevel_(3003),"Closing %s : rx = %llu tx = %llu", c.name.c_str(), c.bytesRx, c.bytesTx);
  theLog->log(LogInfoDevel_(3003),"  data Rx: %s in %.2fs",NumberOfBytesToString(c.bytesRx,"bytes",1024).c_str(),t0);
  theLog->log(LogInfoDevel_(3003),"  rate Rx: %s",NumberOfBytesToString(rateRx*8,"bps").c_str());
  theLog->log(LogInfoDevel_(3003),"  data Tx: %s in %.2fs",NumberOfBytesToString(c.bytesTx,"bytes",1024).c_str(),t0);
  theLog->log(LogInfoDevel_(3003),"  rate Tx: %s",NumberOfBytesToString(rateTx*8,"bps").c_str());
  }
  c.fd=-1;
}

SocketRx::SocketRx(std::string name, int port, AliceO2::InfoLogger::InfoLogger *l, Type st) {
  shutdownRequest=0;  
  serverName=name;
  portNumber=port;
  socketType=st;
  theLog = l;
  
  std::function<void(void)> f = std::bind(&SocketRx::run, this);
  th=std::make_unique<std::thread>(f);
}

SocketRx::~SocketRx(){
  shutdownRequest=1;
  if (th!=nullptr) {
    th->join();
  }
  
  for(auto &c : clients) {
    closeClient(c);
  }
}

void SocketRx::run() {
  #ifdef _GNU_SOURCE
    pthread_setname_np(pthread_self(), "socket-rx");
  #endif

  int sockfd=-1;
  const char *sType="";
  
  if (socketType==Type::TCP) {
    sockfd=socket(AF_INET, SOCK_STREAM, 0);
    sType="TCP";
  } else if (socketType==Type::UDP) {
    sockfd=socket(AF_INET, SOCK_DGRAM, 0);
    sType="UDP";
  } else {
    throw __LINE__;
  }
  if (sockfd == -1) { throw __LINE__; }
  
  int fdopt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &fdopt, sizeof(fdopt)) < 0) { throw __LINE__; }
  
  struct sockaddr_in servaddr; 
  bzero(&servaddr, sizeof(servaddr)); 
  servaddr.sin_family = AF_INET; 
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
  servaddr.sin_port = htons(portNumber); 
  
  if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0) { throw __LINE__; }
  
  char h[100];
  gethostname(h,100);
  serverName+= std::string(" @ ") + h + ":" + std::to_string(portNumber) + " " + sType;
  if (theLog!=nullptr) {
    theLog->log(LogInfoDevel_(3002), "%s listening",serverName.c_str());
  }
  
  if (socketType==Type::TCP) {
    if ((listen(sockfd, 5)) != 0) { throw __LINE__; }

    for (;;) {
      fd_set fds;
      int fdsMax;
      struct timeval tv;
      int result;
      FD_ZERO(&fds);
      FD_SET(sockfd,&fds);
      fdsMax=sockfd;
      tv.tv_sec = 0;
      tv.tv_usec = 100000;
      result=select(fdsMax+1,&fds, NULL, NULL, &tv);
      if (result>0) {
        if (FD_ISSET(sockfd,&fds)) {
          
	  struct sockaddr_in cli;
          socklen_t len=sizeof(cli);
          int connfd = accept(sockfd, (struct sockaddr*)&cli, &len);
          if (theLog!=nullptr) {
            theLog->log(LogInfoDevel_(3002), "%s : %s connected on port %d TCP",
	      serverName.c_str(),inet_ntoa(cli.sin_addr),cli.sin_port);
	  }
	  SocketRxClient c;
	  c.fd = connfd;
	  c.name = std::string(inet_ntoa(cli.sin_addr)) + ":" + std::to_string(cli.sin_port);
	  c.t.reset();
	  c.bytesRx = 0;
	  c.bytesTx = 0;
	  clientsLock.lock();
	  clients.push_back(c);
	  clientsLock.unlock();
        }
      }
      if (shutdownRequest) {
        close(sockfd);
        return;
      }
    }

    

/*    char buf[BUFSZ];

    for(;;) {

      int n=read(connfd, buf, sizeof(buf));
      if (n==0) {break;}
      if (n>0) {
        bytesRx+=n;
      }

      if (shutdownRequest) {
        break;
      }
    }
    close(connfd);
    */

  }
  
  close(sockfd);
  
  /*
  printf("%s : received %llu bytes\n",serverName.c_str(),bytesRx);

  double t0=t.getTime();
  double rate=(bytesRx/t0);
  
  printf("data: %s in %.2fs\n",NumberOfBytesToString(bytesRx,"bytes",1024).c_str(),t0);
  printf("rate: %s\n",NumberOfBytesToString(rate*8,"bps").c_str());
  */
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

void SocketRx::broadcast(const char *message) {
  if (message==nullptr) return;
  ssize_t l = strlen(message);
  clientsLock.lock();
  for(auto it = clients.begin(); it != clients.end();){
    ssize_t rs = send((*it).fd, message, l, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (l != rs) {
      // failed to send, closing
       closeClient(*it);
       it = clients.erase(it);
    } else {
      (*it).bytesTx += l;
      ++it;
    }
  }
  clientsLock.unlock();
}
