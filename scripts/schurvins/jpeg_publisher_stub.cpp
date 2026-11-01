#include "jpeg_publisher.hpp"
#include <stdexcept>
struct JpegPublisher::Impl {};
JpegPublisher::JpegPublisher(TideCamera&,int64_t) {throw std::runtime_error("JPEG ROS publishing requires the native-dds backend");}
JpegPublisher::~JpegPublisher()=default;
void JpegPublisher::finish() {}
