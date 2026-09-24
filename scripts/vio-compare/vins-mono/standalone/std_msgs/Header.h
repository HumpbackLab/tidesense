#pragma once
#include <string>
namespace std_msgs {
struct Time { double value=0; double toSec() const { return value; } };
struct Header { Time stamp; std::string frame_id; };
}
