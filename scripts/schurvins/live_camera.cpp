#include "live_camera.hpp"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
static_assert(sizeof(CameraHeader) == 56, "IPC header layout");
std::string executableDirectory() {
 char buf[4096];
 ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf)-1);
 if (n <= 0 || n >= ssize_t(sizeof(buf)-1)) throw std::runtime_error("Cannot locate executable");
 std::string path(buf, n);
 return path.substr(0, path.find_last_of('/'));
}
LiveCamera::LiveCamera(const std::string& helper, const std::string& ini) {
 int sockets[2];
 if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets))
  throw std::runtime_error("camera socketpair failed");
 child_ = fork();
 if (child_ < 0) { close(sockets[0]); close(sockets[1]); throw std::runtime_error("camera fork failed"); }
 if (!child_) {
  close(sockets[0]);
  // socketpair normally assigns 3/4; dup2 also clears CLOEXEC on fd3.
  if (dup2(sockets[1], 3) < 0) _exit(126);
  if (fcntl(3, F_SETFD, 0) < 0) _exit(126);
  if (sockets[1] != 3) close(sockets[1]);
  execl(helper.c_str(), helper.c_str(), "--fd3", ini.c_str(), (char*)nullptr);
  _exit(127);
 }
 close(sockets[1]); fd_ = sockets[0];
 timeval timeout{5, 0};
 if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
  stop(); throw std::runtime_error("camera socket timeout setup failed");
 }
 try { receiver_ = std::thread(&LiveCamera::receive, this); }
 catch (...) { stop(); throw; }
}
LiveCamera::~LiveCamera() { stop(); }
void LiveCamera::receive() {
 auto readAll = [&](void* dst, size_t size) {
  auto p = static_cast<unsigned char*>(dst);
  while (size) {
   ssize_t n = recv(fd_, p, size, 0);
   if (n < 0 && errno == EINTR) continue;
   if (n <= 0) throw std::runtime_error("Camera stream closed, stalled, or truncated");
   p += n; size -= n;
  }
 };
 try {
  uint64_t previous = 0, previous_right = 0, sequence = 0;
  while (true) {
   LivePair pair;
   readAll(&pair.header, sizeof(pair.header));
   const auto& h = pair.header;
   if (h.magic != CAMERA_MAGIC || h.version != CAMERA_VERSION || h.width != 640 || h.height != 400 ||
       h.reserved || h.flags > CAMERA_FSIN || h.left_ns <= previous || h.right_ns <= previous_right ||
       h.sequence != sequence + 1)
    throw std::runtime_error("Invalid camera header, size, sequence or timestamps");
   uint64_t skew = h.left_ns > h.right_ns ? h.left_ns-h.right_ns : h.right_ns-h.left_ns;
   if (skew > 25000000) throw std::runtime_error("Camera pair skew exceeds half a frame");
   previous = h.left_ns; previous_right = h.right_ns; sequence = h.sequence;
   pair.left.create(h.height, h.width, CV_8UC1); pair.right.create(h.height, h.width, CV_8UC1);
   readAll(pair.left.data, size_t(h.width)*h.height);
   readAll(pair.right.data, size_t(h.width)*h.height);
   std::lock_guard<std::mutex> lock(mutex_);
   if (stopping_) return;
   if (pending_) ++replaced_;
   latest_ = std::move(pair); pending_ = true; ++received_;
   ready_.notify_one();
  }
 } catch (const std::exception& e) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stopping_) error_ = e.what();
  ready_.notify_one();
 }
}
bool LiveCamera::next(LivePair& pair, const volatile sig_atomic_t& interrupted) {
 std::unique_lock<std::mutex> lock(mutex_);
 while (!pending_ && error_.empty() && !interrupted)
  ready_.wait_for(lock, std::chrono::milliseconds(100));
 if (interrupted) return false;
 if (!error_.empty()) throw std::runtime_error(error_);
 pair = std::move(latest_); pending_ = false;
 return true;
}
int LiveCamera::stop() {
 { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
 if (fd_ >= 0) shutdown(fd_, SHUT_RDWR);
 if (receiver_.joinable()) receiver_.join();
 if (fd_ >= 0) { close(fd_); fd_ = -1; }
 if (child_ > 0) {
  kill(child_, SIGTERM);
  int status = 0;
  for (int i = 0; i < 50; ++i) {
   pid_t result = waitpid(child_, &status, WNOHANG);
   if (result == child_ || (result < 0 && errno == ECHILD)) {
    stop_status_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    child_ = -1; return stop_status_;
   }
   usleep(100000);
  }
  std::cerr << "Camera helper did not stop within 5s; killing it (check FSIN state)\n";
  kill(child_, SIGKILL);
  while (waitpid(child_, &status, 0) < 0 && errno == EINTR) {}
  child_ = -1; stop_status_ = 1;
 }
 return stop_status_;
}
void LiveCamera::report() {
 std::lock_guard<std::mutex> lock(mutex_);
 std::cout << "camera_received=" << received_ << " replaced_pending=" << replaced_
           << " pending_capacity=1\n";
}
