// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <errno.h>
#include <iomanip>

#include "Consumer.h"
#include "RdhUtils.h"
#include "ReadoutStats.h"
#include "ReadoutUtils.h"

// a struct to store info related to one file
class FileHandle
{
 public:
  FileHandle(std::string& _path, InfoLogger* _theLog = nullptr, unsigned long long _maxFileSize = 0, int _maxPages = 0)
  {
    theLog = _theLog;
    path = _path;
    counterBytesTotal = 0;
    maxFileSize = _maxFileSize;
    maxPages = _maxPages;
    if (theLog != nullptr) {
      theLog->log(LogInfoDevel_(3007), "Opening file for writing: %s", path.c_str());
    }
    fp = fopen(path.c_str(), "wb");
    if (fp == NULL) {
      if (theLog != nullptr) {
        theLog->log(LogErrorSupport_(3232), "Failed to create file: %s", strerror(errno));
      }
      return;
    }
    isOk = true;
  }

  ~FileHandle() { close(); }

  void close()
  {
    if (fp != NULL) {
      if (theLog != nullptr) {
        theLog->log(LogInfoDevel_(3007), "Closing file %s : %llu bytes (~%s)", path.c_str(), counterBytesTotal, ReadoutUtils::NumberOfBytesToString(counterBytesTotal, "B").c_str());
      }
      fclose(fp);
      fp = NULL;
    }
    isOk = false;
  }

  // write to file
  // data given by 'ptr', number of bytes given by 'size'
  // isPage is a flag telling if the data belongs to a page (for the 'number of pages written' counter)
  // remainingBlockSize is taken into account not to exceed max file size, to avoid starting writing anything if the next write would reach limit return one of the status code below
  enum Status { Success = 0,
                Error = -1,
                FileLimitsReached = 1 };
  FileHandle::Status write(void* ptr, size_t size, bool isPage = false, size_t remainingBlockSize = 0)
  {
    lastWriteBytes = 0; // reset last bytes written
    if (isFull) {
      // report only first occurence of FileLimitsReached
      return Status::Success;
    }
    if ((size <= 0) || (ptr == nullptr)) {
      return Status::Success;
    }
    if ((maxFileSize) && (counterBytesTotal + size + remainingBlockSize > maxFileSize)) {
      if (theLog != nullptr) {
        theLog->log(LogInfoDevel_(3007), "Maximum file size reached");
      }
      isFull = true;
      close();
      return Status::FileLimitsReached;
    }
    if ((maxPages) && (counterPages >= maxPages)) {
      if (theLog != nullptr) {
        theLog->log(LogInfoDevel_(3007), "Maximum number of pages in file reached");
      }
      isFull = true;
      close();
      return Status::FileLimitsReached;
    }
    if (fp == NULL) {
      return Status::Error;
    }
    if (fwrite(ptr, size, 1, fp) != 1) {
      return Status::Error;
    }
    counterBytesTotal += size;
    gReadoutStats.bytesRecorded += size;
    if (isPage) {
      counterPages++;
    }
    lastWriteBytes = size;
    // printf("%s: %llu/%llu bytes %d/%d pages\n",path.c_str(),counterBytesTotal,maxFileSize,counterPages,maxPages);
    return Status::Success;
  }

  bool isFileOk() { return isOk; }

 private:
  std::string path = "";                    // path to the file (final, after variables substitution)
  unsigned long long counterBytesTotal = 0; // number of bytes written to file
  unsigned long long maxFileSize = 0;       // max number of bytes to write to file (0=no limit)
  int counterPages = 0;                     // number of pages received so far
  int maxPages = 0;                         // max number of pages accepted by recorder (0=no limit)
  FILE* fp = NULL;                          // handle to file for I/O
  InfoLogger* theLog = nullptr;             // handle to infoLogger for messages
  bool isFull = false;                      // flag set when maximum file size reached
  bool isOk = false;                        // flag set when file ready for writing
  size_t lastWriteBytes = 0;                // number of bytes last written with success

 public:
  int fileId = 0; // a placeholder for an incremental counter to identify current file Id (when file splitting enabled)
};

// data source tags used in file identifier
struct DataSourceId {
  uint32_t linkId;
  uint16_t equipmentId;
};

// constant for undefined data source
const DataSourceId undefinedDataSourceId = { undefinedLinkId, undefinedEquipmentId };

// comparison operator
bool operator==(const DataSourceId& a, const DataSourceId& b) { return ((a.linkId == b.linkId) && (a.equipmentId == b.equipmentId)); }

// less operator
bool operator<(const DataSourceId& a, const DataSourceId& b) { return (a.equipmentId < b.equipmentId) || ((a.equipmentId == b.equipmentId) && (a.linkId < b.linkId)); }

class ConsumerFileRecorder : public Consumer
{
 public:
  ConsumerFileRecorder(ConfigFile& cfg, std::string cfgEntryPoint) : Consumer(cfg, cfgEntryPoint)
  {

    // configuration parameter: | consumer-fileRecorder-* | fileName | string | | Path to the file where to record data. The following variables are replaced at runtime: ${XXX} -> get variable XXX from environment, %t -> unix timestamp (seconds since epoch), %T -> formatted date/time, %i -> equipment ID of each data chunk (used to write data from different equipments to different output files), %l -> link ID (used to write data from different links to different output files). |
    fileName = cfg.getValue<std::string>(cfgEntryPoint + ".fileName");
    theLog.log(LogInfoDevel_(3002), "Recording path = %s", fileName.c_str());

    // configuration parameter: | consumer-fileRecorder-* | bytesMax | bytes | 0 | Maximum number of bytes to write to each file. Data pages are never truncated, so if writing the full page would exceed this limit, no data from that page is written at all and file is closed. If zero (default), no maximum size set.|
    std::string sMaxBytes;
    if (cfg.getOptionalValue<std::string>(cfgEntryPoint + ".bytesMax", sMaxBytes) == 0) {
      maxFileSize = ReadoutUtils::getNumberOfBytesFromString(sMaxBytes.c_str());
      if (maxFileSize) {
        theLog.log(LogInfoDevel_(3002), "Maximum recording size: %lld bytes", maxFileSize);
      }
    }

    // configuration parameter: | consumer-fileRecorder-* | pagesMax | int | 0 | Maximum number of data pages accepted by recorder. If zero (default), no maximum set.|
    maxFilePages = 0;
    if (cfg.getOptionalValue<int>(cfgEntryPoint + ".pagesMax", maxFilePages) == 0) {
      if (maxFilePages) {
        theLog.log(LogInfoDevel_(3002), "Maximum recording size: %d pages", maxFilePages);
      }
    }

    // configuration parameter: | consumer-fileRecorder-* | dataBlockHeaderEnabled | int | 0 | Enable (1) or disable (0) the writing to file of the internal readout header (Readout DataBlock.h) between the data pages, to easily navigate through the file without RDH decoding. If disabled, the raw data pages received from CRU are written without further formatting. |
    cfg.getOptionalValue(cfgEntryPoint + ".dataBlockHeaderEnabled", recordWithDataBlockHeader, 0);
    theLog.log(LogInfoDevel_(3002), "Recording internal data block headers = %d", recordWithDataBlockHeader);

    // configuration parameter: | consumer-fileRecorder-* | filesMax | int | 1 | If 1 (default), file splitting is disabled: file is closed whenever a limit is reached on a given recording stream. Otherwise, file splitting is enabled: whenever the current file reaches a limit, it is closed an new one is created (with an incremental name). If <=0, an unlimited number of incremental chunks can be created. If non-zero, it defines the maximum number of chunks. The file name is suffixed with chunk number (by default, ".001, .002, ..." at the end of the file name. One may use "%c" in the file name to define where this incremental file counter is printed. |
    filesMax = 1;
    if (cfg.getOptionalValue<int>(cfgEntryPoint + ".filesMax", filesMax) == 0) {
      if (filesMax == 1) {
        theLog.log(LogInfoDevel_(3002), "File splitting disabled");
      } else {
        if (filesMax > 0) {
          theLog.log(LogInfoDevel_(3002), "File splitting enabled - max %d files per stream", filesMax);
        } else {
          theLog.log(LogInfoDevel_(3002), "File splitting enabled - unlimited files");
        }
      }
    }

    // configuration parameter: | consumer-fileRecorder-* | dropEmptyHBFrames | int | 0 | If 1, memory pages are scanned and empty HBframes are discarded, i.e. couples of packets which contain only RDH, the first one with pagesCounter=0 and the second with stop bit set. This setting does not change the content of in-memory data pages, other consumers would still get full data pages with empty packets. This setting is meant to reduce the amount of data recorded for continuous detectors in triggered mode.|
    cfg.getOptionalValue(cfgEntryPoint + ".dropEmptyHBFrames", dropEmptyHBFrames, 0);
    if (dropEmptyHBFrames) {
      if (recordWithDataBlockHeader) {
        theLog.log(LogErrorSupport_(3100), "Incompatible options dropEmptyHBFrames and dataBlockHeaderEnabled");
        throw __LINE__;
      }
      theLog.log(LogInfoSupport_(3002), "Some packets with RDH-only payload will not be recorded to file, option dropEmptyHBFrames is enabled");
    }
  }

  ~ConsumerFileRecorder() {}

  void resetCounters()
  {
    if (defaultFile != nullptr) {
      defaultFile->close();
      defaultFile = nullptr;
    }

    for (auto& kv : filePerSourceMap) {
      kv.second->close();
      kv.second = nullptr;
    }
    filePerSourceMap.clear();

    // reset counters
    recordingEnabled = false;
    perLinkPreviousPacket.clear();
    invalidRDH = 0;
    emptyPacketsDropped = 0;
    packetsRecorded = 0;
  }

  int start()
  {
    Consumer::start();
    resetCounters();

    theLog.log(LogInfoDevel_(3006), "Starting file recorder");
    // check status
    if (createFile() == 0) {
      recordingEnabled = true;
      theLog.log(LogInfoDevel_(3002), "Recording enabled");
    } else {
      theLog.log(LogWarningSupport_(3232), "Recording disabled");
      isError++;
    }
    return 0;
  };

  int stop()
  {
    theLog.log(LogInfoDevel_(3006), "Stopping file recorder");
    if (dropEmptyHBFrames) {
      theLog.log(LogInfoDevel_(3003), "Packets recorded=%lld discarded(empty)=%lld", packetsRecorded, emptyPacketsDropped);
    }

    resetCounters();
    Consumer::stop();
    return 0;
  }

  // create handle to recording file based on configuration
  // optional params:
  // equipmentID: use given equipment Id
  // delayIfSourceId: when set, file is not created immediately
  // getNewFp: if not null, function will copy handle to created file in the given variable
  int createFile(std::shared_ptr<FileHandle>* getNewHandle = nullptr, const DataSourceId& sourceId = undefinedDataSourceId, bool delayIfSourceId = true, int fileId = 1)
  {

    // create the file name according to specified path
    // parse the string, and subst variables:
    // ${XXX} -> get variable XXX from environment
    // %t -> unix timestamp (seconds since epoch)
    // %T -> formatted date/time
    // %i -> equipment ID of each data chunk (used to write data from different equipments to different output files).
    // %l -> link ID of each data chunk (used to write data from different links to different output files).
    // %f -> file number (incremental), when file splitting enabled (empty otherwise)
    std::string newFileName;

    // string for file incremental ID
    char sFileId[11] = "";
    if ((filesMax != 1) && (fileId > 0)) {
      snprintf(sFileId, sizeof(sFileId), "%03d", fileId);
    }

    int parseError = 0;
    for (std::string::iterator it = fileName.begin(); it != fileName.end(); ++it) {
      // subst environment variable
      if (*it == '$') {
        ++it;
        int varNameComplete = 0;
        if (it != fileName.end()) {
          if (*it == '{') {
            std::string varName;

            for (++it; it != fileName.end(); ++it) {
              if (*it == '}') {
                varNameComplete = 1;
                break;
              } else {
                varName += *it;
              }
            }
            if (varNameComplete) {
              const char* val = getenv(varName.c_str());
              if (val != nullptr) {
                newFileName += val;
                // theLog.log(LogDebugTrace, (varName + " = " + val).c_str());
              }
            }
          }
        }
        if (!varNameComplete) {
          parseError++;
        }
      } else if (*it == '%') {
        // escape characters
        ++it;
        if (it != fileName.end()) {
          if (*it == 't') {
            newFileName += std::to_string(std::time(nullptr));
          } else if (*it == 'T') {
            std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            std::stringstream buffer;
            buffer << std::put_time(&tm, "%Y_%m_%d__%H_%M_%S__");
            newFileName += buffer.str();
          } else if (*it == 'i') {
            if (sourceId.equipmentId == undefinedEquipmentId) {
              newFileName += "undefined";
            } else {
              newFileName += std::to_string(sourceId.equipmentId);
            }
            perSourceRecordingFile = true;
            useSourceEquipmentId = true;
          } else if (*it == 'l') {
            if (sourceId.linkId == undefinedLinkId) {
              newFileName += "undefined";
            } else {
              newFileName += std::to_string(sourceId.linkId);
            }
            perSourceRecordingFile = true;
            useSourceLinkId = true;
          } else if (*it == 'f') {
            newFileName += sFileId;
            // clear, write ID once only
            sFileId[0] = 0;
          } else {
            parseError++;
          }
        } else {
          parseError++;
        }
      } else {
        // normal char - copy it
        newFileName += *it;
      }
      if (parseError) {
        break;
      }
    }
    if (parseError) {
      theLog.log(LogErrorSupport_(3102), "Failed to parse recording file path");
      return -1;
    }

    // ensure file ends with file ID, if not written somewhere else already
    newFileName += sFileId;

    if ((fileId > filesMax) && (filesMax >= 1)) {
      theLog.log(LogInfoDevel_(3007), "Maximum number of files reached for this stream");
      return -1;
    }

    if ((perSourceRecordingFile) && (delayIfSourceId)) {
      // delay file creation to arrival of data... equipmentId is not known yet
      theLog.log(LogInfoDevel_(3007), "Per-source recording file selected, opening of file(s) delayed (until data available)");
      return 0;
    }

    // create file handle
    std::shared_ptr<FileHandle> newHandle = std::make_shared<FileHandle>(newFileName, &theLog, maxFileSize, maxFilePages);
    if (newHandle == nullptr) {
      return -1;
    }
    if (!newHandle->isFileOk()) {
      // no need to log a special message, error printed by FileHandle constructor
      return -1;
    }
    newHandle->fileId = fileId;

    // store new handle where appropriate
    if (perSourceRecordingFile) {
      filePerSourceMap[sourceId] = newHandle;
    } else {
      defaultFile = newHandle;
    }

    // return a copy of new file handle
    if (getNewHandle != nullptr) {
      *getNewHandle = newHandle;
    }

    return 0;
  }

  int pushData(DataBlockContainerReference& b)
  {

    // do nothing if recording disabled
    if (!recordingEnabled) {
      return 0;
    }

    // the file handle to be used for this block by default, the main file
    std::shared_ptr<FileHandle> fpUsed;

    // does it depend on equipmentId ?
    DataSourceId sourceId = undefinedDataSourceId;
    if (perSourceRecordingFile) {
      // select appropriate file for recording
      if (useSourceEquipmentId) {
        sourceId.equipmentId = b->getData()->header.equipmentId;
      }
      if (useSourceLinkId) {
        sourceId.linkId = b->getData()->header.linkId;
      }

      // is there already a file for this equipment?
      FilePerSourceMapIterator it;
      it = filePerSourceMap.find(sourceId);
      if (it == filePerSourceMap.end()) {
        createFile(&fpUsed, sourceId, false);
      } else {
        fpUsed = it->second;
      }
    } else {
      fpUsed = defaultFile;
    }

    // make sure we can store the full page in current file ?

    bool countPage = true; // the first write will increment the page counter for this file

    auto writeToFile = [&](void* ptr, size_t size, size_t remainingBlockSize) {
      // two attempts, in case file needs to be incremented
      for (int i = 0; i < 2; i++) {

        // no good file handle, abort recording
        if (fpUsed == nullptr) {
          theLog.logError("No valid file available: will stop recording now");
          throw __LINE__;
        }

        // try to write
        FileHandle::Status status = fpUsed->write(ptr, size, countPage, remainingBlockSize);

        // check if need to move to next file
        if (status == FileHandle::Status::FileLimitsReached) {
          if (filesMax != 1) {
            // let's move to next file chunk
            int fileId = fpUsed->fileId;
            fileId++;
            if ((filesMax < 1) || (fileId <= filesMax)) {
              createFile(&fpUsed, sourceId, false, fileId);
            }
          }
        }

        if (status == FileHandle::Status::Success) {
          countPage = false;
          return;
        }
      }

      theLog.logError("File write error: will stop recording now");
      fpUsed->close();
      throw __LINE__;
    };

    // basic RDH check
    auto checkRdh = [&](RdhHandle& h) {
      std::string errorDescription;
      if (h.validateRdh(errorDescription)) {
        invalidRDH++;
        throw __LINE__;
      }
      return;
    };

    auto isEmptyHBstop = [&](RdhHandle& h) {
      if ((h.getStopBit()) && (h.getHeaderSize() == h.getMemorySize())) {
        return true;
      }
      return false;
    };

    auto isEmptyHBstart = [&](RdhHandle& h) {
      if ((h.getPagesCounter() == 0) && (h.getHeaderSize() == h.getMemorySize())) {
        return true;
      }
      return false;
    };

    try {
      // check we have a valid file handle
      if (fpUsed == nullptr) {
        throw __LINE__;
      }

      // get handle to stored state for this link
      int linkId = b->getData()->header.linkId;
      Packet& previousPacket = perLinkPreviousPacket[linkId];

      // write datablock header, if wanted
      if (recordWithDataBlockHeader) {
        // as-is, some fields like data pointer will not be meaningful in file unless corrected.
        // todo: correct them, e.g. replace data pointer by file offset.
        // In particular, incompatible with dropEmptyHBFrames as size changes.
        writeToFile(&b->getData()->header, (size_t)b->getData()->header.headerSize, (size_t)b->getData()->header.dataSize);
        // datablock header does not count as a page, but we account for the payload size for the next write (possibly one full page)
      }

      // write payload data
      if (!dropEmptyHBFrames) {
        // by default, we write the full payload data
        writeToFile(b->getData()->data, (size_t)b->getData()->header.dataSize, 0);
      } else {
        // we have to check packet by packet and discard empty HBstart/HBstop pairs
        size_t blockSize = b->getData()->header.dataSize;
        uint8_t* baseAddress = (uint8_t*)(b->getData()->data);
        for (size_t pageOffset = 0; pageOffset < blockSize;) {
          // validate RDH
          RdhHandle h(baseAddress + pageOffset);
          try {
            checkRdh(h);
          } catch (...) {
            // stop for this page on first RDH error
            // cleanup stored previous packet
            previousPacket.clear();
            // write previous
            break;
          }

          // check we still have a valid file handle
          if (fpUsed == nullptr) {
            throw __LINE__;
          }

          // is this an empty HBstop following an empty HBstart ?
          if (previousPacket.isEmptyHBStart && isEmptyHBstop(h)) {
            // yes, let's skip it
            previousPacket.clear();
            pageOffset += h.getOffsetNextPacket();
            emptyPacketsDropped += 2;
            continue;
          }

          // write previous packet
          if (previousPacket.address != nullptr) {
            writeToFile(previousPacket.address, previousPacket.size, 0);
            packetsRecorded++;
            previousPacket.clear();
          }

          // is this an empty HBstart ?
          if (isEmptyHBstart(h)) {
            // keep it aside for later
            previousPacket.size = h.getOffsetNextPacket();
            if (pageOffset + h.getOffsetNextPacket() < blockSize) {
              // not end of page, keep a simple reference
              previousPacket.address = baseAddress + pageOffset;
              previousPacket.isCopy = false;
            } else {
              // end of page, keep a copy
              previousPacket.address = malloc(previousPacket.size);
              if (previousPacket.address == nullptr) {
                throw __LINE__;
              }
              memcpy(previousPacket.address, baseAddress + pageOffset, previousPacket.size);
              previousPacket.isCopy = true;
            }
            previousPacket.isEmptyHBStart = true;
          } else {

            // write packet
            // use offsetNextPacket instead of memorySize for file to be consistent
            writeToFile(baseAddress + pageOffset, (size_t)h.getOffsetNextPacket(), 0);
            packetsRecorded++;
          }

          pageOffset += h.getOffsetNextPacket();

          // infinite loop protection, just in case
          if (h.getOffsetNextPacket() == 0) {
            break;
          }
        }
      }
    } catch (...) {
      recordingEnabled = false;
      return -1;
    }

    return 0;
  }

 private:
  std::shared_ptr<FileHandle> defaultFile; // the file to be used by default

  typedef std::map<DataSourceId, std::shared_ptr<FileHandle>> FilePerSourceMap;
  typedef std::map<DataSourceId, std::shared_ptr<FileHandle>>::iterator FilePerSourceMapIterator;
  typedef std::pair<DataSourceId, std::shared_ptr<FileHandle>> FilePerSourcePair;
  FilePerSourceMap filePerSourceMap;   // a map to store a file handle for each data source (equipmentId, linkId)
  bool perSourceRecordingFile = false; // when set, recording file name is based on id(s) of data source (equipmentId, linkId)
  bool useSourceLinkId = false;        // when set, the link ID is used in file name
  bool useSourceEquipmentId = false;   // when set, the equipment ID is used in file name

  bool recordingEnabled = false; // if not set, recording is disabled

  // from configuration
  std::string fileName = "";          // path/filename to be used for recording (may include variables evaluated at runtime, on file creation)
  int recordWithDataBlockHeader = 0;  // if set, internal readout headers are included in file
  unsigned long long maxFileSize = 0; // maximum number of bytes to write (in each file)
  int maxFilePages = 0;               // maximum number of pages to write (in each file)
  int filesMax = 0;                   // maximum number of files to write (for each stream)
  int dropEmptyHBFrames = 0;          // if set, some empty packets are discarded (see logic in code)

  class Packet
  {
   public:
    bool isEmptyHBStart = false;
    void* address = nullptr;
    size_t size = 0;
    bool isCopy = false;
    void clear()
    {
      isEmptyHBStart = false;
      if ((address != nullptr) && (isCopy)) {
        free(address);
      }
      address = nullptr;
      size = 0;
      isCopy = false;
    }
    Packet() {}
    ~Packet() { clear(); }
  };
  std::map<int, Packet> perLinkPreviousPacket; // store last packet per link

  unsigned long long invalidRDH = 0;          // number of invalid RDH found
  unsigned long long emptyPacketsDropped = 0; // number of packets dropped
  unsigned long long packetsRecorded = 0;     // number of packets recorded
};

std::unique_ptr<Consumer> getUniqueConsumerFileRecorder(ConfigFile& cfg, std::string cfgEntryPoint) { return std::make_unique<ConsumerFileRecorder>(cfg, cfgEntryPoint); }
