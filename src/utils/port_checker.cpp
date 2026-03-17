// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/utils/port_checker.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include <array>
#include <cstring>
#include <memory>
#include <sstream>

namespace ravbot {
namespace utils {

bool PortChecker::IsPortAvailable(int port) {
  if (port < 1 || port > 65535) {
    return false;
  }

  // Try to bind to the port
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return false;
  }

  // Set SO_REUSEADDR to avoid "Address already in use" for recently closed ports
  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  bool available = (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
  close(sock);

  return available;
}

int PortChecker::FindAvailablePort(int start_port, int max_attempts) {
  if (start_port < 1 || start_port > 65535) {
    return -1;
  }

  for (int i = 0; i < max_attempts; ++i) {
    int port = start_port + i;
    if (port > 65535) {
      break;
    }

    if (IsPortAvailable(port)) {
      return port;
    }
  }

  return -1;
}

int PortChecker::FindAvailableInstanceSlot(int base_port, int max_instances) {
  if (base_port < 1 || base_port > 65535 - (max_instances * 2)) {
    return -1;
  }

  // Each instance uses 2 consecutive ports: gateway_port and http_port
  // Instance 0: base_port, base_port+1
  // Instance 1: base_port+2, base_port+3
  // Instance N: base_port+(N*2), base_port+(N*2)+1

  for (int instance = 0; instance < max_instances; ++instance) {
    int gateway_port = base_port + (instance * 2);
    int http_port = gateway_port + 1;

    // Check if both ports are available
    if (IsPortAvailable(gateway_port) && IsPortAvailable(http_port)) {
      return instance;
    }
  }

  return -1;
}

std::vector<std::string> PortChecker::GetProcessesUsingPort(int port) {
  std::vector<std::string> processes;

  // Use lsof to find processes using the port
  std::string cmd = "lsof -i :" + std::to_string(port) + " -t 2>/dev/null";

  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return processes;
  }

  std::array<char, 128> buffer;
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    std::string pid(buffer.data());
    // Remove trailing newline
    if (!pid.empty() && pid.back() == '\n') {
      pid.pop_back();
    }
    if (!pid.empty()) {
      processes.push_back(pid);
    }
  }

  pclose(pipe);
  return processes;
}

bool PortChecker::CanBindToPort(int port) {
  // First check if port is available
  if (!IsPortAvailable(port)) {
    return false;
  }

  // Additional check: try to actually bind and listen
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return false;
  }

  int opt = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  bool can_bind = (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);

  if (can_bind) {
    // Try to listen
    can_bind = (listen(sock, 1) == 0);
  }

  close(sock);
  return can_bind;
}

}  // namespace utils
}  // namespace ravbot
