// Device authentication types and utilities

export type DeviceAuthRole = "admin" | "user" | "readonly";

export type DeviceAuthScope = "chat" | "config" | "sessions" | "tools" | "memory";

export interface DeviceAuthEntry {
  deviceId: string;
  token: string;
  role: DeviceAuthRole;
  scopes: DeviceAuthScope[];
  createdAt: number;
  expiresAt?: number;
}

export interface DeviceAuthStore {
  version: 1;
  deviceId: string;
  tokens: Record<string, DeviceAuthEntry>;
}

export function normalizeDeviceAuthRole(role: string | undefined): DeviceAuthRole {
  if (role === "admin" || role === "user" || role === "readonly") {
    return role;
  }
  return "user";
}

export function normalizeDeviceAuthScopes(scopes: string[] | undefined): DeviceAuthScope[] {
  if (!Array.isArray(scopes)) {
    return ["chat", "config", "sessions", "tools", "memory"];
  }

  const validScopes: DeviceAuthScope[] = ["chat", "config", "sessions", "tools", "memory"];
  return scopes.filter((s): s is DeviceAuthScope =>
    validScopes.includes(s as DeviceAuthScope)
  );
}
