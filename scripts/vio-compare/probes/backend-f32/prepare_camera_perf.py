#!/usr/bin/env python3
"""Add opt-in perf FIFO gating to an isolated fisheye replay adapter."""
import argparse
from pathlib import Path


HEADER = r'''
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

static bool camera_perf_gate = false;
static int camera_perf_start = 0;
static int camera_perf_control = -1;
static int camera_perf_ack = -1;
static int camera_perf_last_state = -1;
static void set_camera_perf(bool enabled) {
  if (!camera_perf_gate) return;
  if (camera_perf_last_state == static_cast<int>(enabled)) return;
  const std::string command = enabled ? "enable\n" : "disable\n";
  size_t sent = 0;
  while (sent < command.size()) {
    const ssize_t result = write(camera_perf_control, command.data() + sent, command.size() - sent);
    if (result < 0 && errno == EINTR) continue;
    if (result <= 0) throw std::runtime_error("perf control write failed");
    sent += static_cast<size_t>(result);
  }
  std::string ack;
  for (;;) {
    char ch;
    const ssize_t result = read(camera_perf_ack, &ch, 1);
    if (result < 0 && errno == EINTR) continue;
    if (result != 1) throw std::runtime_error("perf acknowledgement read failed");
    if (ch == '\0') continue; // Linux 5.10 perf writes sizeof("ack\n"), including NUL.
    if (ch == '\n') break;
    ack += ch;
    if (ack.size() > 32) throw std::runtime_error("invalid perf acknowledgement");
  }
  if (ack != "ack") throw std::runtime_error("unexpected perf acknowledgement: " + ack);
  camera_perf_last_state = enabled;
}
'''


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f"expected one adapter boundary: {old[:80]}")
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise SystemExit("perf adapter must be separate from the normal adapter")
    text = args.source.read_text()
    text = replace_once(text, "int main(int argc, char **argv) {", HEADER + "\nint main(int argc, char **argv) {")
    text = replace_once(text, "  try {\n    const std::string record = argv[2];", '''  try {
    const char *gate = std::getenv("OPENVINS_CAMERA_PERF_GATE");
    camera_perf_gate = gate && std::strcmp(gate, "1") == 0;
    const char *start = std::getenv("OPENVINS_CAMERA_PERF_START");
    if (start) camera_perf_start = std::stoi(start);
    if (camera_perf_gate) {
      const char *control = std::getenv("OPENVINS_CAMERA_PERF_CTL");
      const char *ack = std::getenv("OPENVINS_CAMERA_PERF_ACK");
      if (!control || !ack) throw std::runtime_error("perf control FIFO paths missing");
      camera_perf_control = open(control, O_WRONLY);
      camera_perf_ack = open(ack, O_RDONLY);
      if (camera_perf_control < 0 || camera_perf_ack < 0)
        throw std::runtime_error("cannot open perf control FIFOs");
    }
    set_camera_perf(false);
    const std::string record = argv[2];''')
    text = replace_once(text, "      const auto start = std::chrono::steady_clock::now();", '''      set_camera_perf(frame_count >= camera_perf_start);
      const auto start = std::chrono::steady_clock::now();''')
    text = replace_once(text, "      const auto end = std::chrono::steady_clock::now();", '''      const auto end = std::chrono::steady_clock::now();
      set_camera_perf(false);''')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == "__main__":
    main()
