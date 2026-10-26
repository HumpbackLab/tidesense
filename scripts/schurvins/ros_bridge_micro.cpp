// SPDX-License-Identifier: GPL-3.0-or-later
#include "ros_bridge.hpp"
#include "micro_ros_bridge.hpp"
struct RosBridge::Impl {MicroRosBridge transport;};
RosBridge::RosBridge():impl_(new Impl) {}
RosBridge::~RosBridge()=default;
bool RosBridge::compiled() {return MicroRosBridge::compiled();}
const char* RosBridge::backend() {return compiled()?"micro-ros":"none";}
bool RosBridge::enabled() const {return impl_->transport.enabled();}
void RosBridge::submit(uint64_t t,bool tracking,const double* p,const double* q) {impl_->transport.submit(t,tracking,p,q);}
