// Session key parsing utility
export function parseAgentSessionKey(sessionKey: string): { agentId?: string } | null {
  if (!sessionKey) return null;

  const parts = sessionKey.split(':');
  if (parts[0] !== 'agent') return null;

  return {
    agentId: parts[1] || undefined
  };
}
