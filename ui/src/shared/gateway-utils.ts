// Gateway client types
export type GatewayClientMode = "webchat" | "api" | "cli";
export type GatewayClientName = "control-ui" | "web-client" | "cli-client";

export const GATEWAY_CLIENT_MODES = {
  WEBCHAT: "webchat" as GatewayClientMode,
  API: "api" as GatewayClientMode,
  CLI: "cli" as GatewayClientMode,
};

export const GATEWAY_CLIENT_NAMES = {
  CONTROL_UI: "control-ui" as GatewayClientName,
  WEB_CLIENT: "web-client" as GatewayClientName,
  CLI_CLIENT: "cli-client" as GatewayClientName,
};

// Tool display utilities
export interface ToolDisplayInfo {
  name: string;
  icon?: string;
  category?: string;
}

export function getToolDisplayInfo(toolName: string): ToolDisplayInfo {
  return {
    name: toolName,
    icon: "🔧",
    category: "general"
  };
}

export function defaultTitle(name: string): string {
  return name.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
}

export function normalizeVerb(verb: string): string {
  return verb.toLowerCase().trim();
}

export function resolveActionSpec(toolName: string): unknown {
  return { name: toolName };
}

export function resolveDetailFromKeys(data: Record<string, unknown>, keys: string[]): string {
  for (const key of keys) {
    if (data[key]) return String(data[key]);
  }
  return "";
}

export function resolveExecDetail(params: Record<string, unknown>): string {
  return String(params.command || "");
}

export function resolveReadDetail(params: Record<string, unknown>): string {
  return String(params.file_path || params.path || "");
}

export function resolveWebFetchDetail(params: Record<string, unknown>): string {
  return String(params.url || "");
}

export function resolveWebSearchDetail(params: Record<string, unknown>): string {
  return String(params.query || params.search_query || "");
}

export function resolveWriteDetail(params: Record<string, unknown>): string {
  return String(params.file_path || params.path || "");
}

export function normalizeToolName(name: string): string {
  return name.trim().toLowerCase().replace(/[^a-z0-9_-]/g, '_');
}

export interface ToolDisplaySpec {
  title?: string;
  icon?: string;
  category?: string;
}

// Gateway device auth
export function buildDeviceAuthPayload(deviceId: string, token: string): Record<string, unknown> {
  return {
    deviceId,
    token,
    timestamp: Date.now()
  };
}

// Client info
export interface ClientInfo {
  userAgent: string;
  platform: string;
  version: string;
}

export function getClientInfo(): ClientInfo {
  return {
    userAgent: navigator.userAgent,
    platform: navigator.platform,
    version: "1.0.0"
  };
}

// Connect error details
export function readConnectErrorDetailCode(error: unknown): string | null {
  if (typeof error === "object" && error !== null && "code" in error) {
    return String(error.code);
  }
  return null;
}

// Time formatting
export function formatDurationHuman(ms: number): string {
  if (ms < 1000) return `${ms}ms`;
  const sec = Math.floor(ms / 1000);
  if (sec < 60) return `${sec} second${sec !== 1 ? 's' : ''}`;
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min} minute${min !== 1 ? 's' : ''}`;
  const hr = Math.floor(min / 60);
  if (hr < 24) return `${hr} hour${hr !== 1 ? 's' : ''}`;
  const day = Math.floor(hr / 24);
  return `${day} day${day !== 1 ? 's' : ''}`;
}

export function formatRelativeTimestamp(timestamp: number): string {
  const now = Date.now();
  const diff = now - timestamp;

  if (diff < 60000) return "just now";
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`;
  if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`;
  return `${Math.floor(diff / 86400000)}d ago`;
}

// Text utilities
export function stripReasoningTagsFromText(text: string): string {
  return text.replace(/<think>[\s\S]*?<\/think>/gi, '').trim();
}

// Gateway events
export const GATEWAY_EVENT_UPDATE_AVAILABLE = "update_available";
