#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
namespace ros {
struct Time { double sec; explicit Time(double value = 0) : sec(value) {} double toSec() const { return sec; } };
struct NodeHandle { template<class T> bool getParam(const std::string&, T&) { return false; } void shutdown() {} };
}
#define ROS_INFO(...) do { std::fprintf(stderr, "INFO: "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ROS_WARN(...) do { std::fprintf(stderr, "WARN: "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while(0)
#define ROS_ERROR(...) ROS_WARN(__VA_ARGS__)
#define ROS_DEBUG(...) do {} while(0)
#define ROS_INFO_STREAM(x) do { std::cerr << "INFO: " << x << '\n'; } while(0)
#define ROS_WARN_STREAM(x) do { std::cerr << "WARN: " << x << '\n'; } while(0)
#define ROS_ERROR_STREAM(x) do { std::cerr << "ERROR: " << x << '\n'; } while(0)
#define ROS_DEBUG_STREAM(x) do {} while(0)
#define ROS_ASSERT(x) assert(x)
#define ROS_ASSERT_MSG(x, ...) assert(x)
#define ROS_BREAK() std::abort()
