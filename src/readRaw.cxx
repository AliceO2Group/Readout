#include <stdio.h>

#include <Common/DataBlock.h>
#include <Common/DataBlockContainer.h>
#include <Common/DataSet.h>

#include "RdhUtils.h"




typedef struct {
  uint32_t w0zero : 32;
  uint32_t w1zero : 32;
  uint16_t w2zero : 12;
  uint16_t headerSize : 8;
  uint16_t linkId : 8;
  uint16_t feeId : 16;
  uint16_t blockLength : 16;
  uint16_t headerVersion : 4;
} RDHa;



int main(int argc, const char *argv[]) {

  // parse input arguments
  if (argc<2) {
    printf("Usage: %s rawFilePath\n",argv[0]);
    return -1;
  }
  const char *filePath=argv[1];

  // open raw data file
  FILE *fp=fopen(filePath,"rb");
  if (fp==NULL) {
    printf("Failed to open %s\n",filePath);
    return -1;
  }
  printf("Reading %s\n",filePath);
  
  // read file
  unsigned long pageCount=0;
  for(long fileOffset=0;;) {
  
    #define ERR_LOOP {printf("Error %d @ 0x%08lX\n",__LINE__,fileOffset); break;}
 
    long blockOffset=fileOffset;
    
    DataBlockHeaderBase hb;
    if (fread(&hb,sizeof(hb),1,fp)!=1) {break;}
    fileOffset+=sizeof(hb);
    
    if (hb.blockType!=DataBlockType::H_BASE) {ERR_LOOP;}
    if (hb.headerSize!=sizeof(hb)) {ERR_LOOP;}
    
    void *data=malloc(hb.dataSize);
    if (data==NULL) {ERR_LOOP;}
    if (fread(data,hb.dataSize,1,fp)!=1) {ERR_LOOP;}
    fileOffset+=hb.dataSize;
    
    /*
    printf("Block %ld @ 0x%08lX\n",hb.id,blockOffset);
    
    RDH *wPtr=(RDH *)data;
    for (int i=0;i<RDH_WORDS;i++) {
      printf("w[%d] = 0x%08X\n", i, wPtr->words[i]);
    }
    
    printf("RDH = %ld bytes\n",sizeof(RDHa));
    RDHa *hPtr=(RDHa *)data;
    
    printf("headerSize=%d\n",(int)hPtr->headerSize);
    printf("linkId=%d\n",(int)hPtr->linkId);
    printf("feeId=%d\n",(int)hPtr->feeId);
    printf("blockLength=%d\n",(int)hPtr->blockLength);
    printf("headerVersion=%d\n",(int)hPtr->headerVersion);
    printf("linkId=%d\n",(int)((wPtr->words[2]>>4 ) & 0xFF));
    */

    std::string errorDescription;    
    for (size_t pageOffset=0;pageOffset<hb.dataSize;) {
      pageCount++;
      RdhHandle h(((uint8_t *)data)+pageOffset);
      if (h.validateRdh(errorDescription)) {
        h.dumpRdh();
        printf("File offset 0x%08lX + %ld\n%s",blockOffset,pageOffset,errorDescription.c_str());
        errorDescription.clear();
      }
      pageOffset+=(h.getBlockLength())*(size_t)32; // length counted in 256b words
    }
    
    free(data);
  }
  printf("%ld data pages\n",pageCount);
  
  // check file status
  if (feof(fp)) {
    printf("End of file\n");
  }
  
  // close file
  fclose(fp);
}
