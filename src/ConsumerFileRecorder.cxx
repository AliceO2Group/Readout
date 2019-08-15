// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "Consumer.h"
#include "ReadoutUtils.h"
#include "ReadoutStats.h"
#include <errno.h>
#include <iomanip>

// a struct to store info related to one file
class FileHandle {
public:
  FileHandle(std::string &_path, InfoLogger *_theLog = nullptr,
             unsigned long long _maxFileSize = 0, int _maxPages = 0) {
    theLog = _theLog;
    path = _path;
    counterBytesTotal = 0;
    maxFileSize = _maxFileSize;
    maxPages = _maxPages;
    if (theLog != nullptr) {
      theLog->log("Opening file for writing: %s", path.c_str());
    }
    fp = fopen(path.c_str(), "wb");
    if (fp == NULL) {
      if (theLog != nullptr) {
        theLog->log(InfoLogger::Severity::Error, "Failed to create file: %s",
                    strerror(errno));
      }
      return;
    }
    isOk = true;
  }

  ~FileHandle() { close(); }

  void close() {
    if (fp != NULL) {
      if (theLog != nullptr) {
        theLog->log("Closing file %s : %llu bytes (~%s)", path.c_str(),
                    counterBytesTotal,
                    ReadoutUtils::NumberOfBytesToString(counterBytesTotal, "B")
                        .c_str());
      }
      fclose(fp);
      fp = NULL;
    }
    isOk = false;
  }

  // write to file
  // data given by 'ptr', number of bytes given by 'size'
  // isPage is a flag telling if the data belongs to a page (for the 'number of
  // pages written' counter) remainingBlockSize is taken into account not to
  // exceed max file size, to avoid starting writing anything if the next write
  // would reach limit return one of the status code below
  enum Status { Success = 0, Error = -1, FileLimitsReached = 1 };
  FileHandle::Status write(void *ptr, size_t size, bool isPage = false,
                           size_t remainingBlockSize = 0) {
    if (isFull) {
      // report only first occurence of FileLimitsReached
      return Status::Success;
    }
    if ((size <= 0) || (ptr == nullptr)) {
      return Status::Success;
    }
    if ((maxFileSize) &&
        (counterBytesTotal + size + remainingBlockSize > maxFileSize)) {
      if (theLog != nullptr) {
        theLog->log("Maximum file size reached");
      }
      isFull = true;
      close();
      return Status::FileLimitsReached;
    }
    if ((maxPages) && (counterPages >= maxPages)) {
      if (theLog != nullptr) {
        theLog->log("Maximum number of pages in file reached");
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
    // printf("%s: %llu/%llu bytes %d/%d
    // pages\n",path.c_str(),counterBytesTotal,maxFileSize,counterPages,maxPages);
    return Status::Success;
  }

  bool isFileOk() { return isOk; }

private:
  std::string path =
      ""; // path to the file (final, after variables substitution)
  unsigned long long counterBytesTotal = 0; // number of bytes written to file
  unsigned long long maxFileSize =
      0;                // max number of bytes to write to file (0=no limit)
  int counterPages = 0; // number of pages received so far
  int maxPages = 0;     // max number of pages accepted by recorder (0=no limit)
  FILE *fp = NULL;      // handle to file for I/O
  InfoLogger *theLog = nullptr; // handle to infoLogger for messages
  bool isFull = false;          // flag set when maximum file size reached
  bool isOk = false;            // flag set when file ready for writing

public:
  int fileId = 0; // a placeholder for an incremental counter to identify
                  // current file Id (when file splitting enabled)
};

// data source tags used in file identifier
struct DataSourceId {
  uint32_t linkId;
  uint16_t equipmentId;
};

// constant for undefined data source
const DataSourceId undefinedDataSourceId = {undefinedLinkId,
                                            undefinedEquipmentId};

// comparison operator
bool operator==(const DataSourceId &a, const DataSourceId &b) {
  return ((a.linkId == b.linkId) && (a.equipmentId == b.equipmentId));
}

// less operator
bool operator<(const DataSourceId &a, const DataSourceId &b) {
  return (a.equipmentId < b.equipmentId) ||
         ((a.equipmentId == b.equipmentId) && (a.linkId < b.linkId));
}

class ConsumerFileRecorder : public Consumer {
public:
  ConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint)
      : Consumer(cfg, cfgEntryPoint) {

    // configuration parameter: | consumer-fileRecorder-* | fileName | string |
    // | Path to the file where to record data. The following variables are
    // replaced at runtime: ${XXX} -> get variable XXX from environment, %t ->
    // unix timestamp (seconds since epoch), %T -> formatted date/time, %i ->
    // equipment ID of each data chunk (used to write data from different
    // equipments to different output files), %l -> link ID (used to write data
    // from different links to different output files). |
    fileName = cfg.getValue<std::string>(cfgEntryPoint + ".fileName");
    theLog.log("Recording path = %s", fileName.c_str());

    // configuration parameter: | consumer-fileRecorder-* | bytesMax | bytes | 0
    // | Maximum number of bytes to write to each file. Data pages are never
    // truncated, so if writing the full page would exceed this limit, no data
    // from that page is written at all and file is closed. If zero (default),
    // no maximum size set.|
    std::string sMaxBytes;
    if (cfg.getOptionalValue<std::string>(cfgEntryPoint + ".bytesMax",
                                          sMaxBytes) == 0) {
      maxFileSize = ReadoutUtils::getNumberOfBytesFromString(sMaxBytes.c_str());
      if (maxFileSize) {
        theLog.log("Maximum recording size: %lld bytes", maxFileSize);
      }
    }

    // configuration parameter: | consumer-fileRecorder-* | pagesMax | int | 0 |
    // Maximum number of data pages accepted by recorder. If zero (default), no
    // maximum set.|
    maxFilePages = 0;
    if (cfg.getOptionalValue<int>(cfgEntryPoint + ".pagesMax", maxFilePages) ==
        0) {
      if (maxFilePages) {
        theLog.log("Maximum recording size: %d pages", maxFilePages);
      }
    }

    // configuration parameter: | consumer-fileRecorder-* |
    // dataBlockHeaderEnabled | int | 0 | Enable (1) or disable (0) the writing
    // to file of the internal readout header (Common::DataBlockHeaderBase
    // struct) between the data pages, to easily navigate through the file
    // without RDH decoding. If disabled, the raw data pages received from CRU
    // are written without further formatting. |
    cfg.getOptionalValue(cfgEntryPoint + ".dataBlockHeaderEnabled",
                         recordWithDataBlockHeader, 0);
    theLog.log("Recording internal data block headers = %d",
               recordWithDataBlockHeader);

    // configuration parameter: | consumer-fileRecorder-* | filesMax | int | 1 |
    // If 1 (default), file splitting is disabled: file is closed whenever a
    // limit is reached on a given recording stream. Otherwise, file splitting
    // is enabled: whenever the current file reaches a limit, it is closed an
    // new one is created (with an incremental name). If <=0, an unlimited
    // number of incremental chunks can be created. If non-zero, it defines the
    // maximum number of chunks. The file name is suffixed with chunk number (by
    // default, ".001, .002, ..." at the end of the file name. One may use "%c"
    // in the file name to define where this incremental file counter is
    // printed. |
    filesMax = 1;
    if (cfg.getOptionalValue<int>(cfgEntryPoint + ".filesMax", filesMax) == 0) {
      if (filesMax == 1) {
        theLog.log("File splitting disabled");
      } else {
        if (filesMax > 0) {
          theLog.log("File splitting enabled - max %d files per stream",
                     filesMax);
        } else {
          theLog.log("File splitting enabled - unlimited files");
        }
      }
    }

    // check status
    if (createFile() == 0) {
      recordingEnabled = true;
      theLog.log("Recording enabled");
    } else {
      theLog.log(InfoLogger::Severity::Warning, "Recording disabled");
      isError++;
    }
  }

  ~ConsumerFileRecorder() {
    if (defaultFile != nullptr) {
      defaultFile->close();
      defaultFile = nullptr;
    }

    for (auto &kv : filePerSourceMap) {
      kv.second->close();
      kv.second = nullptr;
    }
    filePerSourceMap.clear();
  }

  // create handle to recording file based on configuration
  // optional params:
  // equipmentID: use given equipment Id
  // delayIfSourceId: when set, file is not created immediately
  // getNewFp: if not null, function will copy handle to created file in the
  // given variable
  int createFile(std::shared_ptr<FileHandle> *getNewHandle = nullptr,
                 const DataSourceId &sourceId = undefinedDataSourceId,
                 bool delayIfSourceId = true, int fileId = 1) {

    // create the file name according to specified path
    // parse the string, and subst variables:
    // ${XXX} -> get variable XXX from environment
    // %t -> unix timestamp (seconds since epoch)
    // %T -> formatted date/time
    // %i -> equipment ID of each data chunk (used to write data from different
    // equipments to different output files). %l -> link ID of each data chunk
    // (used to write data from different links to different output files). %f
    // -> file number (incremental), when file splitting enabled (empty
    // otherwise)
    std::string newFileName;

    // string for file incremental ID
    char sFileId[4] = "";
    if ((filesMax != 1) && (fileId > 0)) {
      snprintf(sFileId, sizeof(sFileId), "%03d", fileId);
    }

    int parseError = 0;
    for (std::string::iterator it = fileName.begin(); it != fileName.end();
         ++it) {
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
              const char *val = getenv(varName.c_str());
              if (val != nullptr) {
                newFileName += val;
                // theLog.log((varName + " = " + val).c_str());
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
      theLog.log(InfoLogger::Severity::Error,
                 "Failed to parse recording file path");
      return -1;
    }

    // ensure file ends with file ID, if not written somewhere else already
    newFileName += sFileId;

    if ((fileId > filesMax) && (filesMax >= 1)) {
      theLog.log(InfoLogger::Severity::Info,
                 "Maximum number of files reached for this stream");
      return -1;
    }

    if ((perSourceRecordingFile) && (delayIfSourceId)) {
      // delay file creation to arrival of data... equipmentId is not known yet
      // !
      theLog.log("Per-source recording file selected, opening of file(s) "
                 "delayed (until data available)");
      return 0;
    }

    // create file handle
    std::shared_ptr<FileHandle> newHandle = std::make_shared<FileHandle>(
        newFileName, &theLog, maxFileSize, maxFilePages);
    if (newHandle == nullptr) {
      return -1;
    }
    if (!newHandle->isFileOk()) {
      // no need to log a special message, error printed by FileHandle
      // constructor
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

  int pushData(DataBlockContainerReference &b) {

    // do nothing if recording disabled
    if (!recordingEnabled) {
      return 0;
    }

    // the file handle to be used for this block
    // by default, the main file
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

    // we have several attempts to write the file (e.g. in case it needs to be
    // incremented)
    for (int i = 0; i < 2; i++) {

      if (fpUsed == nullptr) {
        theLog.logError("No valid file available: will stop recording now");
        recordingEnabled = false;
        return -1;
      }

      void *ptr = nullptr;
      size_t size = 0;
      FileHandle::Status status = FileHandle::Status::Success;
      if ((status == FileHandle::Status::Success) &&
          (recordWithDataBlockHeader)) {
        // write header
        // as-is, some fields like data pointer will not be meaningful in file
        // unless corrected. todo: correct them, e.g. replace data pointer by
        // file offset.
        ptr = &b->getData()->header;
        size = b->getData()->header.headerSize;
        status = fpUsed->write(
            ptr, size, false,
            b->getData()->header.dataSize); // header does not count as a page,
                                            // but we account for the payload
                                            // size for the next write
      }
      if (status == FileHandle::Status::Success) {
        // write payload data
        ptr = b->getData()->data;
        size = b->getData()->header.dataSize;
        status = fpUsed->write(ptr, size, true); // payload count as a page
      }
      if (status == FileHandle::Status::Success) {
        return 0;
      }
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
    }

    theLog.logError("File write error: will stop recording now");
    recordingEnabled = false;
    fpUsed->close();
    return -1;
  }

private:
  std::shared_ptr<FileHandle> defaultFile; // the file to be used by default

  typedef std::map<DataSourceId, std::shared_ptr<FileHandle>> FilePerSourceMap;
  typedef std::map<DataSourceId, std::shared_ptr<FileHandle>>::iterator
      FilePerSourceMapIterator;
  typedef std::pair<DataSourceId, std::shared_ptr<FileHandle>>
      FilePerSourcePair;
  FilePerSourceMap filePerSourceMap; // a map to store a file handle for each
                                     // data source (equipmentId, linkId)
  bool perSourceRecordingFile =
      false; // when set, recording file name is based on id(s) of data source
             // (equipmentId, linkId)
  bool useSourceLinkId = false; // when set, the link ID is used in file name
  bool useSourceEquipmentId =
      false; // when set, the equipment ID is used in file name

  bool recordingEnabled = false; // if not set, recording is disabled

  // from configuration
  std::string fileName =
      ""; // path/filename to be used for recording (may include variables
          // evaluated at runtime, on file creation)
  int recordWithDataBlockHeader =
      0; // if set, internal readout headers are included in file
  unsigned long long maxFileSize =
      0;                // maximum number of bytes to write (in each file)
  int maxFilePages = 0; // maximum number of pages to write (in each file)
  int filesMax = 0;     // maximum number of files to write (for each stream)
};

std::unique_ptr<Consumer>
getUniqueConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFileRecorder>(cfg, cfgEntryPoint);
}
