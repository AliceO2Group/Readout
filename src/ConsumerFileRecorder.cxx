#include "Consumer.h"
#include "ReadoutUtils.h"
#include <iomanip>

class ConsumerFileRecorder: public Consumer {
  public: 
  
  ConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    counterBytesTotal=0;
    fp=NULL;
    
    fileName=cfg.getValue<std::string>(cfgEntryPoint + ".fileName");
    if (fileName.length()>0) {
      theLog.log("Recording path = %s",fileName.c_str());
      createFile();
    }
    if (fp==NULL) {
      theLog.log("Recording disabled");
    } else {
      theLog.log("Recording enabled");
    }
    
    std::string sMaxBytes;
    if (cfg.getOptionalValue<std::string>(cfgEntryPoint + ".bytesMax",sMaxBytes)==0) {
      counterBytesMax=ReadoutUtils::getNumberOfBytesFromString(sMaxBytes.c_str());
      if (counterBytesMax) {
        theLog.log("Maximum recording size: %lld bytes",counterBytesMax);
      }
    }
    
    cfg.getOptionalValue(cfgEntryPoint + ".dataBlockHeaderEnabled", recordWithDataBlockHeader, 0);
    theLog.log("Recording internal data block headers = %d",recordWithDataBlockHeader);
  }
  ~ConsumerFileRecorder() {
    closeRecordingFile();
  }
  
  int createFile() {
    // create the file name according to specified path
    
    // parse the string, and subst variables:
    // ${XXX} -> get variable XXX from environment
    // %t -> unix timestamp (seconds since epoch)
    // %T -> formatted date/time
    
    std::string newFileName;
    
    int parseError=0;
    for (std::string::iterator it=fileName.begin();it!=fileName.end();++it) {
      // subst environment variable
      if (*it=='$') {
        ++it;
        int varNameComplete=0;
        if (it!=fileName.end()) {
          if (*it=='{') {
            std::string varName;

            for (++it;it!=fileName.end();++it) {
              if (*it=='}') {
                varNameComplete=1;
                break;
              } else {
                varName+=*it;
              }
            }
            if (varNameComplete) {
              const char *val=getenv(varName.c_str());
              if (val!=nullptr) {
                newFileName+=val;
                //theLog.log((varName + " = " + val).c_str());
              }
            }
          }
        }
        if (!varNameComplete) {
          parseError++;
        }
      } else if (*it=='%') {
        // escape characters
        ++it;
        if (it!=fileName.end()) {
          if (*it=='t') {
            newFileName+=std::to_string(std::time(nullptr));
          } else if (*it=='T') {
            std::time_t t = std::time(nullptr);
            std::tm tm = *std::localtime(&t);
            std::stringstream buffer;
            buffer << std::put_time(&tm, "%Y_%m_%d__%H_%M_%S__");
            newFileName+=buffer.str();
          } else {
            parseError++;
          }
        } else {
          parseError++;
        }      
      } else {
        // normal char - copy it
        newFileName+=*it;
      }   
      if (parseError) {
        break;
      }
    }  
    if (parseError) {
      theLog.log("Failed to parse recording file path");
      return -1;
    }
    theLog.log(("Opening file for writing: " + newFileName).c_str());
    
    fp=fopen(newFileName.c_str(),"wb");
    if (fp==NULL) {
      theLog.log("Failed to create file");
      return -1;
    }
    return 0;
  }
  
  // fwrite function with partial write auto-retry
  int writeToFile(FILE *fp, void *data, size_t numberOfBytes) {
    unsigned char *buffer=(unsigned char *)data;
    for (int i=0;i<1024;i++) {
      //theLog.log("write %ld @ %lp",numberOfBytes,buffer);
      size_t bytesWritten=fwrite(buffer,1,numberOfBytes,fp);
      //if (bytesWritten<0) {break;}
      if (bytesWritten>numberOfBytes) {break;}
      if (bytesWritten==0) {usleep(1000);}
      if (bytesWritten==numberOfBytes) {return 0;}
      numberOfBytes-=bytesWritten;
      buffer=&buffer[bytesWritten];
    }
    return -1;
  }
  
  int pushData(DataBlockContainerReference &b) {

    for(;;) {
      if (fp!=NULL) {
        void *ptr=nullptr;
        size_t size=0;
        if (recordWithDataBlockHeader) {
          // write header
          // as-is, some fields like data pointer will not be meaningful in file unless corrected. todo: correct them, e.g. replace data pointer by file offset.
          ptr=&b->getData()->header;
          size=b->getData()->header.headerSize;
          //theLog.log("Writing header: %ld bytes @ %lp",(long)size,ptr);
          if ((counterBytesMax)&&(counterBytesTotal+size>counterBytesMax)) {theLog.log("Maximum file size reached"); closeRecordingFile(); return 0;}
          //if (writeToFile(fp,ptr,size)) {
          if (fwrite(ptr,size,1,fp)!=1) {
            break;
          }
          counterBytesTotal+=size;
        }
        // write payload data     
        ptr=b->getData()->data;
        size=b->getData()->header.dataSize;
        //theLog.log("Writing payload: %ld bytes @ %lp",(long)size,ptr);
        if ((counterBytesMax)&&(counterBytesTotal+size>counterBytesMax)) {theLog.log("Maximum file size reached"); closeRecordingFile(); return 0;}
        if ((size>0)&&(ptr!=nullptr)) {
          //if (writeToFile(fp,ptr,size)) {
          if (fwrite(ptr,size,1,fp)!=1) {
            break;
          }
        }
        counterBytesTotal+=size;
        //theLog.log("File size: %ld bytes",(long)counterBytesTotal);
      }
      return 0;
    }
    theLog.log("File write error");
    closeRecordingFile();
    return -1;
  }
  private:
    unsigned long long counterBytesTotal;
    unsigned long long counterBytesMax=0;
    FILE *fp;
    int recordingEnabled;
    int recordWithDataBlockHeader=0; // if set, internal readout headers are included in file
    std::string fileName;
    void closeRecordingFile() {
      if (fp!=NULL) {
        theLog.log("Closing %s",fileName.c_str());
        fclose(fp);
        fp=NULL;
      }
    }
};


std::unique_ptr<Consumer> getUniqueConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_unique<ConsumerFileRecorder>(cfg, cfgEntryPoint);
}
