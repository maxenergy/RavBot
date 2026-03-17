// Message metadata stripping utility
export function stripInboundMetadata(message: string): string {
  // Remove any metadata markers from inbound messages
  return message.replace(/\[METADATA:.*?\]/g, '').trim();
}

// Chat envelope utilities
export function stripEnvelope(message: string): string {
  // Remove envelope wrapper if present
  return message.replace(/^<envelope>|<\/envelope>$/g, '').trim();
}

// Usage aggregates
export function buildUsageAggregateTail(data: unknown[]): unknown {
  return data.slice(-10); // Return last 10 items
}

// Time formatting
export function formatDurationCompact(ms: number): string {
  if (ms < 1000) return `${ms}ms`;
  const sec = Math.floor(ms / 1000);
  if (sec < 60) return `${sec}s`;
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min}m`;
  const hr = Math.floor(min / 60);
  return `${hr}h`;
}
