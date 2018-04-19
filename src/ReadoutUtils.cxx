#include "ReadoutUtils.h"
#include <math.h>


#include "RAWDataHeader.h"


#include <Configuration/Visitor.h>
#include <Configuration/Tree.h>

// function to convert a string to a 64-bit integer value
// allowing usual "base units" in suffix (k,M,G,T)
// input can be decimal (1.5M is valid, will give 1.5*1024*1024)
long long ReadoutUtils::getNumberOfBytesFromString(const char * inputString) {
  double v=0;
  char c='?';
  int n=sscanf(inputString,"%lf%c",&v,&c);
  
  if (n==1) {
    return (long long)v;
  }
  if (n==2) {
    if (c=='k') {
      return (long long)(v*1024LL);
    }
    if (c=='M') {
      return (long long)(v*1024LL*1024LL);
    }
    if (c=='G') {
      return (long long)(v*1024LL*1024LL*1024LL);
    }
    if (c=='T') {
      return (long long)(v*1024LL*1024LL*1024LL*1024LL);
    }    
    if (c=='P') {
      return (long long)(v*1024LL*1024LL*1024LL*1024LL*1024LL);
    }    
  }
  return 0;
}

// macro to get number of element in static array
#define STATIC_ARRAY_ELEMENT_COUNT(x) sizeof(x)/sizeof(x[0]) 

std::string ReadoutUtils::NumberOfBytesToString(double value,const char*suffix) {
  const char *prefixes[]={"","k","M","G","T","P"};
  int maxPrefixIndex=STATIC_ARRAY_ELEMENT_COUNT(prefixes)-1;
  int prefixIndex=log(value)/log(1024);
  if (prefixIndex>maxPrefixIndex) {
    prefixIndex=maxPrefixIndex;
  }
  if (prefixIndex<0) {
    prefixIndex=0;
  }
  double scaledValue=value/pow(1024,prefixIndex);
  char bufStr[64];
  if (suffix==nullptr) {
    suffix="";
  }
  snprintf(bufStr,sizeof(bufStr)-1,"%.03lf %s%s",scaledValue,prefixes[prefixIndex],suffix);
  return std::string(bufStr);  
}

void convertConfigurationNodeToPTree(const o2::configuration::tree::Node& node, boost::property_tree::ptree &pt, std::string basePath="", const char separator='.')
{
  o2::configuration::visitor::apply(node,
      [&](const o2::configuration::tree::Branch& branch) {
        if (basePath.length()!=0) {basePath+=separator;}
        for (const auto& keyValuePair : branch) {
          convertConfigurationNodeToPTree(keyValuePair.second, pt, basePath+keyValuePair.first);
        }
      },
      [&](const o2::configuration::tree::Leaf& leaf) {
        std::string value= o2::configuration::tree::convert<std::string>(leaf);
	pt.put(basePath,value);
      }
  );
}


void dumpRDH(o2::Header::RAWDataHeader *rdh) {
  printf("RDH:\tversion=%d\theader size=%d\tblock length=%d\n",(int)rdh->version,(int)rdh->headerSize,(int)rdh->blockLength);
  printf("\tTRG orbit=%d bc=%d\n",(int)rdh->triggerOrbit,(int)rdh->triggerBC);
  printf("\tHB  orbit=%d bc=%d\n",(int)rdh->heartbeatOrbit,(int)rdh->heartbeatBC);
  printf("\tfeeId=%d\tlinkId=%d\n",(int)rdh->feeId,(int)rdh->linkId);
}
