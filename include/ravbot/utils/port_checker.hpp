// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace ravbot {
namespace utils {

// Port availability checker
class PortChecker {
 public:
  // Check if a port is available (not in use)
  static bool IsPortAvailable(int port);

  // Find an available port starting from the given port
  // Returns the available port, or -1 if no port found in range
  static int FindAvailablePort(int start_port, int max_attempts = 100);

  // Find an available instance slot for multi-instance deployment
  // Each instance uses 2 consecutive ports (gateway + http)
  // Returns the instance number (0-based), or -1 if no slot available
  // base_port: starting port for instance 0 (default: 18800)
  // max_instances: maximum number of instances to support (default: 20)
  static int FindAvailableInstanceSlot(int base_port = 18800, int max_instances = 20);

  // Get list of processes using a specific port
  static std::vector<std::string> GetProcessesUsingPort(int port);

  // Check if current process can bind to the port
  static bool CanBindToPort(int port);
};

}  // namespace utils
}  // namespace ravbot
