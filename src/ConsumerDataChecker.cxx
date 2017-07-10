#include "Consumer.h"

typedef struct {
  uint32_t w0;
  uint32_t w1;
  uint32_t w2;
  uint32_t payloadSize;
  uint32_t w4;
  uint32_t w5;
  uint32_t w6;
  uint32_t w7;
  uint32_t w8;
  uint32_t w9;
  uint32_t w10;
  uint32_t w11;
  uint32_t w12;
  uint32_t w13;
  uint32_t w14;
  uint32_t w15;
} RocPageHeader;


class ConsumerDataChecker: public Consumer {
  public:
  
  uint32_t checkValue;
  unsigned long long errorCount;
  unsigned long long checkedPages;
  
  ConsumerDataChecker(ConfigFile &cfg, std::string cfgEntryPoint):Consumer(cfg,cfgEntryPoint) {
    checkValue=0;  // internal data generator starts 0 and increases every 256bits word
    errorCount=0;
    checkedPages=0;    
  }  
  ~ConsumerDataChecker() {
    theLog.log("Checker detected %llu data errors on %llu DMA pages",errorCount,checkedPages);
  }
  int pushData(DataBlockContainerReference b) {
  
    void *ptr;    
    size_t size;
    ptr=b->getData()->data;
    if (ptr==NULL) {return -1;}
    
    size=b->getData()->header.dataSize;   
//    theLog.log("checking container %p data @ %p : %d bytes",(void *)b.get(),ptr,(int)size);

    unsigned int pageId=0;    
    for(unsigned int i=0;i<size;pageId++) {
      checkedPages++;
      RocPageHeader *h=(RocPageHeader *)&(((char *)ptr)[i]);

/*      printf("page %u @ %p\n",pageId,(void *)h);
      for (unsigned int w=0;w<sizeof(RocPageHeader)/sizeof(unsigned int);w++) {
	printf("%08X  ",(((unsigned int *)h)[w]));
	if (w%8==7) printf("\n");
      }   
      printf("\n");
*/
      void *pagePayloadPtr=&((char*)h)[sizeof(RocPageHeader)];
      unsigned int pagePayloadSize=(h->payloadSize) * 256 / 8 - sizeof(RocPageHeader);  // convert to bytes the size given in number of 256-bits words

/*
      printf("payload size = %u\n",pagePayloadSize);
      for (unsigned int w=0;w<pagePayloadSize/sizeof(unsigned int);w++) {
	printf("%08X  ",(((unsigned int *)pagePayloadPtr)[w]));
	if (w%8==7) printf("\n");
      }
      printf("\n");

      for (unsigned int w=0;w<16;w++) {
	printf("%08X  ",(((unsigned int *)pagePayloadPtr)[w]));
	if (w%8==7) printf("\n");
      }
*/

      // check counter increasing
      /*
      for (unsigned int w=0;w<pagePayloadSize/sizeof(unsigned int);w++) {
        if (((unsigned int *)pagePayloadPtr)[w]!=checkValue) {
	  errorCount++;
	  if ((errorCount<100)||(errorCount%1000==0)) {
  	    theLog.log("Error #%llu : Superpage %p Page %d : 32-bit word %d mismatch : %X != %X\n",errorCount,ptr,i,w,((unsigned int *)pagePayloadPtr)[w],checkValue);
	  }

	}
        if ((w%8)==7) {checkValue++;}
      }
      */

      for (unsigned int w=0;w<pagePayloadSize/sizeof(unsigned int);w+=8) {
        if (
	  (((unsigned int *)pagePayloadPtr)[w]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+1]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+2]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+3]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+4]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+5]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+6]!=checkValue) ||
	  (((unsigned int *)pagePayloadPtr)[w+7]!=checkValue) )
	 {
	  errorCount++;
	  if ((errorCount<100)||(errorCount%1000==0)) {
  	    theLog.log("Error #%llu : Superpage %p Page %d (size %d) : 32-bit word %d mismatch : %X != %X\n",errorCount,ptr,pageId,pagePayloadSize,w,((unsigned int *)pagePayloadPtr)[w],checkValue);
	  }

	}
        checkValue++;
      }

      
      //printf("page %d (size %d) checked\n",pageId,pagePayloadSize);
      
 
      i+=8*1024;
    }
    //printf("superpage %p checked\n",ptr);
    
/*    if (size>sizeof(RocPageHeader)) {
      RocPageHeader *h=(RocPageHeader *)&ptr;
      printWord(&h->words[0]);
      printWord(&h->words[1]);
      printWord(&h->words[2]);	// trick! this is to print beginning of payload...
//      uint8_t *payload=((uint8_t *)ptr)+sizeof(RocPageHeader);
    }
  */  
    

    return 0;
  }
  private:
};


std::shared_ptr<Consumer> getSharedConsumerDataChecker(ConfigFile &cfg, std::string cfgEntryPoint) {
  return std::make_shared<ConsumerDataChecker>(cfg, cfgEntryPoint);
}
