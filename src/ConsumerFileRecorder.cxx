#include "Consumer.h"



class ConsumerFileRecorder: public Consumer {
  public: 
  ConsumerFileRecorder(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    counterBytesTotal=0;
    fp=NULL;
    
    fileName=cfg.getValue<std::string>(cfgEntryPoint + ".fileName");
    if (fileName.length()>0) {
      theLog.log("Recording to %s",fileName.c_str());
      fp=fopen(fileName.c_str(),"wb");
      if (fp==NULL) {
        theLog.log("Failed to create file");
      }
    }
    if (fp==NULL) {
      theLog.log("Recording disabled");
    } else {
      theLog.log("Recording enabled");
    }   
  }  
  ~ConsumerFileRecorder() {
    closeRecordingFile();
  }
  int pushData(DataBlockContainerReference &b) {

    for(;;) {
      if (fp!=NULL) {
        void *ptr;
        size_t size;

        ptr=&b->getData()->header;
        size=b->getData()->header.headerSize;
        if (fwrite(ptr,size, 1, fp)!=1) {
          break;
        }     
        counterBytesTotal+=size;
        ptr=&b->getData()->data;
        size=b->getData()->header.dataSize; 
        if ((size>0)&&(ptr!=nullptr)) {
          if (fwrite(ptr,size, 1, fp)!=1) {
            break;
          }
        }
        counterBytesTotal+=size;
      }
      return 0;
    }
    closeRecordingFile();
    return -1;
  }
  private:
    unsigned long long counterBytesTotal;
    FILE *fp;
    int recordingEnabled;
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
