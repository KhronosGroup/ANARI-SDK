//
// MaterialXCpp graph types — pxr-independent representation of a
// material network (mirrors HdMaterialNetwork2 structure).
//
#ifndef MXCPP_GRAPH_TYPES_H
#define MXCPP_GRAPH_TYPES_H

#include "value.h"

#include <map>
#include <string>
#include <vector>

namespace mxcpp {

struct GraphConnection {
    std::string upstreamNode;
    std::string upstreamOutputName;
};

struct GraphNode {
    std::string nodeTypeId;
    std::map<std::string, Value> parameters;
    std::map<std::string, std::vector<GraphConnection>> inputConnections;
};

struct MaterialGraph {
    std::map<std::string, GraphNode> nodes;
    std::map<std::string, GraphConnection> terminals;
};

}  // namespace mxcpp

#endif  // MXCPP_GRAPH_TYPES_H
