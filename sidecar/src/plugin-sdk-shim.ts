// Copyright 2024 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// Plugin SDK shim — provides the "openclaw/plugin-sdk" module alias.
//
// OpenClaw plugins do:
//   import { emptyPluginConfigSchema, type OpenClawPluginApi } from "openclaw/plugin-sdk";
//
// This module re-exports compatible types and helpers so those imports work.
// ---------------------------------------------------------------------------

// Re-export all types plugins may reference.
export type {
  PluginApi as OpenClawPluginApi,
  AgentTool as AnyAgentTool,
  ToolFactory as OpenClawPluginToolFactory,
  ToolRegistrationOptions as OpenClawPluginToolOptions,
  ToolFactoryContext as OpenClawPluginToolContext,
  HookHandler as InternalHookHandler,
  HookOptions as OpenClawPluginHookOptions,
  HttpHandler as OpenClawPluginHttpHandler,
  HttpHandler as OpenClawPluginHttpRouteHandler,
  CliRegistrar as OpenClawPluginCliRegistrar,
  PluginService as OpenClawPluginService,
  ServiceContext as OpenClawPluginServiceContext,
  PluginCommand as OpenClawPluginCommandDefinition,
  ProviderPlugin,
  ChannelPlugin,
  PluginLogger,
  PluginRuntime,
  PluginDefinition as OpenClawPluginDefinition,
  GatewayMethodHandler as GatewayRequestHandler,
} from "./types.js";

// ---------------------------------------------------------------------------
// Helpers used by plugins
// ---------------------------------------------------------------------------

/**
 * Returns an empty config schema that always validates.
 * Many plugins use this when they have no configuration.
 */
export function emptyPluginConfigSchema(): {
  safeParse: (value: unknown) => { success: true; data: Record<string, unknown> };
  jsonSchema: Record<string, unknown>;
} {
  return {
    safeParse: (_value: unknown) => ({
      success: true as const,
      data: {} as Record<string, unknown>,
    }),
    jsonSchema: {
      type: "object",
      additionalProperties: false,
      properties: {},
    },
  };
}

/**
 * Resolve a user-provided path, expanding ~ to home directory.
 */
export function resolveUserPath(input: string): string {
  if (input.startsWith("~")) {
    const home = process.env.HOME ?? process.env.USERPROFILE ?? "/tmp";
    return input.replace(/^~/, home);
  }
  return input;
}
