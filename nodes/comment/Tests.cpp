#include "Node.hpp"
#include <cassert>
#include <cstdio>
#include <array>
using namespace tri;
int main(){const auto module=tri::nodes::comment::module();assert(module.spec.type==nodeTypeID("soundnet.comment")&&!module.spec.legacyAdapter&&!module.spec.kernelFactory&&module.spec.inputs.empty()&&module.spec.outputs.empty());puts("PASS comment: pure metadata contract");}
