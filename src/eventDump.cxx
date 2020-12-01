#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>
using namespace AliceO2::InfoLogger;

#include "ZmqClient.hxx"
#include "TtyChecker.h"
#include "RdhUtils.h"

// set log environment before theLog is initialized
// use console output, non-blocking input
TtyChecker theTtyChecker;

// log handle
InfoLogger theLog;

int main(int argc, const char* argv[])
{
  std::string port = "tcp://127.0.0.1:50001"; // ZMQ server address
  int pageSize = 2 * 1024L * 1024L;           // ZMQ RX buffer size, should be big enough to receive a full superpage
  int maxRdhPerPage = 0;                      // set maximum number of RDH printed per page. 0 means all.

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
    if (key == "maxRdhPerPage") {
      maxRdhPerPage = atoi(value.c_str());
    }
  }

  theLog.log(LogInfoOps, "Starting eventDump");
  theLog.log(LogInfoDevel, "Connecting to %s, page size = %d, maxRdhPerPage = %d", port.c_str(), pageSize, maxRdhPerPage);
  theLog.log(LogInfoOps, "Interactive keyboard commands: (s) start (d) stop (n) next page (x) exit");

  std::unique_ptr<ZmqClient> tfClient;
  tfClient = std::make_unique<ZmqClient>(port, pageSize);

  if (tfClient == nullptr) {
    theLog.log(LogErrorOps, "Failed to connect");
  }

  int pageCount = 0;

  int maxPages = 0;

  auto processMessage = [&](void* msg, int msgSize) {
    pageCount++;
    printf("# Page %d - %d bytes\n", pageCount, msgSize);

    std::string errorDescription;
    int lines = 0;
    for (size_t pageOffset = 0; pageOffset < (unsigned long)msgSize;) {
      lines++;
      if ((maxRdhPerPage > 0) && (lines > maxRdhPerPage))
        break;

      if (pageOffset + sizeof(o2::Header::RAWDataHeader) > (unsigned long)msgSize) {
        break;
      }
      void* rdh = &((uint8_t*)msg)[pageOffset];
      RdhHandle h(rdh);
      h.dumpRdh(pageOffset, 1);
      // go to next RDH
      uint16_t offsetNextPacket = h.getOffsetNextPacket();
      if (offsetNextPacket == 0) {
        break;
      }
      pageOffset += offsetNextPacket;
    }

    if ((maxPages > 0) && (pageCount >= maxPages)) {
      tfClient->setPause(1);
    }

    return 0;
  };

  tfClient->setPause(1);
  tfClient->setCallback(processMessage);

  bool shutdown = 0;
  for (; !shutdown;) {
    int c = getchar();
    if (c > 0) {
      switch (c) {
        case 'x':
          shutdown = 1;
          break;
        case 'n':
          pageCount = 0;
          maxPages = 1;
          tfClient->setPause(0);
          break;
        case 's':
          pageCount = 0;
          maxPages = 0;
          tfClient->setPause(0);
          break;
        case 'd':
          tfClient->setPause(1);
          break;
        default:
          break;
      }
    }
    usleep(10000);
  }

  tfClient->setCallback(nullptr);
  tfClient->setPause(0);

  theLog.log(LogInfoOps, "Exiting");
  return 0;
}
