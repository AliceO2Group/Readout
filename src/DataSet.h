#include "DataBlockContainer.h"
#include <vector>
#include <memory>


/*
  A container to store a vector of shared pointers to data block containers.
  Can typically be used to pass a set of blocks to a function.
*/

using DataBlockContainerReference=std::shared_ptr<DataBlockContainer>;

using DataSet=std::vector<DataBlockContainerReference>;
using DataSetReference=std::shared_ptr<DataSet>;


/* TBD */
/*
class DataSetHelper {
};
*/
