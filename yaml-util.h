#include <yaml-cpp/yaml.h>

#ifndef YAML_UTIL
#define YAML_UTIL

namespace YAML {
template<>
struct convert<Node> {
	static Node encode(const Node& rhs) {return rhs;}
	static bool decode(const Node& node, Node& rhs) {rhs=node; return true;}
};
} //> namespace YAML

#endif //YAML_UTIL
