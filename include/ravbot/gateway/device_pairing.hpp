// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace ravbot::gateway {

// Device pairing request
struct PairingRequest {
    std::string request_id;
    std::string device_id;
    std::string device_name;
    std::string role;  // "operator" | "node"
    std::vector<std::string> scopes;
    int64_t requested_at_ms;
    std::string client_info;

    nlohmann::json ToJson() const {
        return {
            {"requestId", request_id},
            {"deviceId", device_id},
            {"deviceName", device_name},
            {"role", role},
            {"scopes", scopes},
            {"requestedAtMs", requested_at_ms},
            {"clientInfo", client_info}
        };
    }
};

// Device token
struct DeviceToken {
    std::string role;
    std::string token;
    std::vector<std::string> scopes;
    int64_t created_at_ms;
    int64_t rotated_at_ms;

    nlohmann::json ToJson() const {
        nlohmann::json j = {
            {"role", role},
            {"scopes", scopes},
            {"createdAtMs", created_at_ms}
        };
        if (rotated_at_ms > 0) {
            j["rotatedAtMs"] = rotated_at_ms;
        }
        return j;
    }
};

// Paired device
struct PairedDevice {
    std::string device_id;
    std::string device_name;
    std::string role;
    std::vector<std::string> approved_scopes;
    std::map<std::string, DeviceToken> tokens;  // role -> token
    int64_t paired_at_ms;

    nlohmann::json ToJson(bool include_tokens = false) const {
        nlohmann::json j = {
            {"deviceId", device_id},
            {"deviceName", device_name},
            {"role", role},
            {"approvedScopes", approved_scopes},
            {"pairedAtMs", paired_at_ms}
        };

        if (include_tokens) {
            nlohmann::json tokens_json = nlohmann::json::object();
            for (const auto& [role, token] : tokens) {
                tokens_json[role] = token.ToJson();
            }
            j["tokens"] = tokens_json;
        } else {
            // Summarize tokens without exposing actual token values
            nlohmann::json tokens_summary = nlohmann::json::array();
            for (const auto& [role, token] : tokens) {
                tokens_summary.push_back({
                    {"role", role},
                    {"scopes", token.scopes}
                });
            }
            j["tokens"] = tokens_summary;
        }

        return j;
    }
};

// Device pairing manager
class DevicePairingManager {
public:
    DevicePairingManager();
    ~DevicePairingManager();

    // List all pairing requests and paired devices
    nlohmann::json ListPairing() const;

    // Approve a pairing request
    std::optional<PairingRequest> ApprovePairing(const std::string& request_id);

    // Reject a pairing request
    std::optional<PairingRequest> RejectPairing(const std::string& request_id);

    // Remove a paired device
    std::optional<PairedDevice> RemovePairedDevice(const std::string& device_id);

    // Rotate device token
    std::optional<DeviceToken> RotateDeviceToken(
        const std::string& device_id,
        const std::string& role,
        const std::vector<std::string>& scopes);

    // Revoke device token
    bool RevokeDeviceToken(const std::string& device_id, const std::string& role);

    // Add a pairing request (for testing/simulation)
    void AddPairingRequest(const PairingRequest& request);

private:
    std::vector<PairingRequest> pending_requests_;
    std::vector<PairedDevice> paired_devices_;

    std::string GenerateToken() const;
    PairingRequest* FindPendingRequest(const std::string& request_id);
    PairedDevice* FindPairedDevice(const std::string& device_id);
};

} // namespace ravbot::gateway
