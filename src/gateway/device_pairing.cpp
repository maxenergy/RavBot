// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/gateway/device_pairing.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace quantclaw::gateway {

DevicePairingManager::DevicePairingManager() = default;
DevicePairingManager::~DevicePairingManager() = default;

std::string DevicePairingManager::GenerateToken() const {
    // Generate a random token (simplified implementation)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::stringstream ss;
    ss << "qc_";
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return ss.str();
}

PairingRequest* DevicePairingManager::FindPendingRequest(const std::string& request_id) {
    auto it = std::find_if(pending_requests_.begin(), pending_requests_.end(),
        [&](const PairingRequest& req) { return req.request_id == request_id; });
    return it != pending_requests_.end() ? &(*it) : nullptr;
}

PairedDevice* DevicePairingManager::FindPairedDevice(const std::string& device_id) {
    auto it = std::find_if(paired_devices_.begin(), paired_devices_.end(),
        [&](const PairedDevice& dev) { return dev.device_id == device_id; });
    return it != paired_devices_.end() ? &(*it) : nullptr;
}

nlohmann::json DevicePairingManager::ListPairing() const {
    nlohmann::json pending = nlohmann::json::array();
    for (const auto& req : pending_requests_) {
        pending.push_back(req.ToJson());
    }

    nlohmann::json paired = nlohmann::json::array();
    for (const auto& dev : paired_devices_) {
        paired.push_back(dev.ToJson(false));  // Don't include actual tokens
    }

    return {
        {"pending", pending},
        {"paired", paired}
    };
}

std::optional<PairingRequest> DevicePairingManager::ApprovePairing(const std::string& request_id) {
    auto* request = FindPendingRequest(request_id);
    if (!request) {
        return std::nullopt;
    }

    // Create paired device
    PairedDevice device;
    device.device_id = request->device_id;
    device.device_name = request->device_name;
    device.role = request->role;
    device.approved_scopes = request->scopes;
    device.paired_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Generate initial token
    DeviceToken token;
    token.role = request->role;
    token.token = GenerateToken();
    token.scopes = request->scopes;
    token.created_at_ms = device.paired_at_ms;
    token.rotated_at_ms = 0;

    device.tokens[request->role] = token;

    // Save the request before removing it
    PairingRequest approved_request = *request;

    // Remove from pending and add to paired
    paired_devices_.push_back(device);
    pending_requests_.erase(
        std::remove_if(pending_requests_.begin(), pending_requests_.end(),
            [&](const PairingRequest& req) { return req.request_id == request_id; }),
        pending_requests_.end());

    return approved_request;
}

std::optional<PairingRequest> DevicePairingManager::RejectPairing(const std::string& request_id) {
    auto* request = FindPendingRequest(request_id);
    if (!request) {
        return std::nullopt;
    }

    PairingRequest rejected_request = *request;

    // Remove from pending
    pending_requests_.erase(
        std::remove_if(pending_requests_.begin(), pending_requests_.end(),
            [&](const PairingRequest& req) { return req.request_id == request_id; }),
        pending_requests_.end());

    return rejected_request;
}

std::optional<PairedDevice> DevicePairingManager::RemovePairedDevice(const std::string& device_id) {
    auto* device = FindPairedDevice(device_id);
    if (!device) {
        return std::nullopt;
    }

    PairedDevice removed_device = *device;

    // Remove from paired devices
    paired_devices_.erase(
        std::remove_if(paired_devices_.begin(), paired_devices_.end(),
            [&](const PairedDevice& dev) { return dev.device_id == device_id; }),
        paired_devices_.end());

    return removed_device;
}

std::optional<DeviceToken> DevicePairingManager::RotateDeviceToken(
    const std::string& device_id,
    const std::string& role,
    const std::vector<std::string>& scopes) {

    auto* device = FindPairedDevice(device_id);
    if (!device) {
        return std::nullopt;
    }

    // Generate new token
    DeviceToken new_token;
    new_token.role = role;
    new_token.token = GenerateToken();
    new_token.scopes = scopes.empty() ? device->approved_scopes : scopes;
    new_token.created_at_ms = device->tokens.count(role) > 0
        ? device->tokens[role].created_at_ms
        : std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    new_token.rotated_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    device->tokens[role] = new_token;

    return new_token;
}

bool DevicePairingManager::RevokeDeviceToken(const std::string& device_id, const std::string& role) {
    auto* device = FindPairedDevice(device_id);
    if (!device) {
        return false;
    }

    auto it = device->tokens.find(role);
    if (it == device->tokens.end()) {
        return false;
    }

    device->tokens.erase(it);
    return true;
}

void DevicePairingManager::AddPairingRequest(const PairingRequest& request) {
    pending_requests_.push_back(request);
}

} // namespace quantclaw::gateway
