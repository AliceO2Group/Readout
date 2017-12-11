#include "Consumer.h"

int Consumer::pushData(DataSetReference &bc) {
   unsigned int success=0;
   for (auto &b : *bc) {
     if (!pushData(b)) {
        success++;
     }
   }
   return success;
}
