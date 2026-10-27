#pragma once
#include <atomic>
#include <memory>
#include <string>
class Downloads {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 Downloads(const std::string& prefix,std::atomic<bool>& stopped);
 ~Downloads();
 std::string prepare(const std::string& directory,const std::string& recording,const std::string& token);
 void release(const std::string& token);
 // 0: preview route; 1: socket handed to file worker; -1: rejected, caller closes.
 int serve(int socket,const std::string& target);
};
