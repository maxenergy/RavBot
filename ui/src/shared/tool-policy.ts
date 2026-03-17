// Tool name normalization utility
export function normalizeToolName(name: string): string {
  return name.trim().toLowerCase().replace(/[^a-z0-9_-]/g, '_');
}

// Tool groups expansion
export function expandToolGroups(patterns: string[]): string[] {
  const expanded: string[] = [];
  for (const pattern of patterns) {
    // Handle tool groups like @web, @file, etc.
    if (pattern.startsWith('@')) {
      const group = pattern.slice(1);
      switch (group) {
        case 'web':
          expanded.push('fetch', 'search', 'browse');
          break;
        case 'file':
          expanded.push('read', 'write', 'edit');
          break;
        case 'exec':
          expanded.push('exec', 'bash', 'shell');
          break;
        default:
          expanded.push(pattern);
      }
    } else {
      expanded.push(pattern);
    }
  }
  return expanded;
}

// Tool profile options
export const PROFILE_OPTIONS = [
  { value: "coding", label: "Coding" },
  { value: "general", label: "General" },
  { value: "research", label: "Research" },
  { value: "custom", label: "Custom" },
] as const;

// Tool catalog types
export interface ToolDefinition {
  name: string;
  description?: string;
  parameters?: Record<string, unknown>;
}

export interface ToolSection {
  id: string;
  label: string;
  tools: string[];
}

export function listCoreToolSections(): ToolSection[] {
  return [
    {
      id: "file",
      label: "File Operations",
      tools: ["read", "write", "edit", "glob", "grep"]
    },
    {
      id: "exec",
      label: "Execution",
      tools: ["exec", "bash", "shell"]
    },
    {
      id: "web",
      label: "Web & Network",
      tools: ["fetch", "search", "browse"]
    },
    {
      id: "agent",
      label: "Agent Tools",
      tools: ["agent", "subagent", "message"]
    }
  ];
}

export interface ToolPolicy {
  allow?: string[];
  deny?: string[];
}

export function resolveToolProfilePolicy(profile: string): ToolPolicy | null {
  switch (profile) {
    case "coding":
      return {
        allow: ["*"],
        deny: []
      };
    case "general":
      return {
        allow: ["read", "write", "search", "fetch"],
        deny: ["exec", "bash", "shell"]
      };
    case "research":
      return {
        allow: ["read", "search", "fetch", "browse"],
        deny: ["write", "exec"]
      };
    default:
      return null;
  }
}

export const BUILTIN_TOOLS: ToolDefinition[] = [];
