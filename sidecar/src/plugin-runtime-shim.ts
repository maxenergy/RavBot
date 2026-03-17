// Copyright 2024 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

// ---------------------------------------------------------------------------
// Minimal PluginRuntime shim.
//
// OpenClaw's PluginRuntime exposes ~100+ methods covering every subsystem
// (channels, media, TTS, etc.).  For the sidecar MVP we implement only the
// essentials and stub everything else so plugins can load without crashing.
// ---------------------------------------------------------------------------

import * as fs from "node:fs";
import * as path from "node:path";
import * as child_process from "node:child_process";
import type { PluginLogger, PluginRuntime } from "./types.js";

const VERSION = "0.1.0";

function notImplemented(name: string): (...args: unknown[]) => never {
  return () => {
    throw new Error(`[ravbot-sidecar] ${name} is not implemented`);
  };
}

function stubNamespace(names: string[]): Record<string, (...args: unknown[]) => unknown> {
  const ns: Record<string, (...args: unknown[]) => unknown> = {};
  for (const n of names) {
    ns[n] = notImplemented(n);
  }
  return ns;
}

export function createPluginRuntime(opts: {
  config: Record<string, unknown>;
  workspaceDir?: string;
  logger: PluginLogger;
}): PluginRuntime {
  const { config, workspaceDir, logger } = opts;

  const homeDir = process.env.HOME ?? process.env.USERPROFILE ?? "/tmp";
  const ravbotDir = path.join(homeDir, ".ravbot");

  return {
    version: VERSION,

    config: {
      loadConfig: () => ({ ...config }),
      writeConfigFile: (_patch) => {
        logger.warn("[runtime] writeConfigFile is not yet implemented in sidecar");
      },
    },

    system: {
      enqueueSystemEvent: (_event) => {
        logger.debug?.("[runtime] enqueueSystemEvent (no-op in sidecar)");
      },

      runCommandWithTimeout: async (cmd, args, runOpts) => {
        const timeout = (runOpts as { timeoutMs?: number } | undefined)?.timeoutMs ?? 30_000;
        return new Promise((resolve) => {
          const child = child_process.spawn(cmd, args, {
            timeout,
            stdio: ["ignore", "pipe", "pipe"],
          });
          let stdout = "";
          let stderr = "";
          child.stdout?.on("data", (d: Buffer) => { stdout += d.toString(); });
          child.stderr?.on("data", (d: Buffer) => { stderr += d.toString(); });
          child.on("close", (code) => {
            resolve({ stdout, stderr, exitCode: code ?? 1 });
          });
          child.on("error", (err) => {
            resolve({ stdout, stderr, exitCode: 1 });
          });
        });
      },

      formatNativeDependencyHint: (dep) => `apt install ${dep}`,
    },

    logging: {
      shouldLogVerbose: () => !!process.env.RAVBOT_VERBOSE,
      getChildLogger: (bindings, childOpts) => {
        const prefix = bindings
          ? `[${Object.values(bindings).join(":")}]`
          : "[plugin]";
        return {
          debug: (msg: string) => logger.debug?.(`${prefix} ${msg}`),
          info: (msg: string) => logger.info(`${prefix} ${msg}`),
          warn: (msg: string) => logger.warn(`${prefix} ${msg}`),
          error: (msg: string) => logger.error(`${prefix} ${msg}`),
        };
      },
    },

    state: {
      resolveStateDir: (pluginId) => {
        const dir = path.join(ravbotDir, "plugins", pluginId);
        fs.mkdirSync(dir, { recursive: true });
        return dir;
      },
    },

    // Stubs — plugins that need these will get clear errors.
    media: stubNamespace([
      "loadWebMedia", "detectMime", "mediaKindFromMime",
      "isVoiceCompatibleAudio", "getImageMetadata", "resizeToJpeg",
    ]),
    tts: stubNamespace(["textToSpeechTelephony"]),
    tools: stubNamespace([
      "createMemoryGetTool", "createMemorySearchTool", "registerMemoryCli",
    ]),
    channel: {
      text: stubNamespace(["send", "reply", "typing"]),
      reply: stubNamespace(["send"]),
      routing: stubNamespace(["resolve"]),
      pairing: stubNamespace(["generate", "verify", "isPaired"]),
      media: stubNamespace(["send"]),
      activity: stubNamespace(["typing"]),
      session: stubNamespace(["resolve", "get"]),
      mentions: stubNamespace(["parse"]),
      reactions: stubNamespace(["add", "remove"]),
      groups: stubNamespace(["list", "info"]),
      debounce: stubNamespace(["queue"]),
      commands: stubNamespace(["register"]),
      discord: stubNamespace(["send", "reply"]),
      slack: stubNamespace(["send", "reply"]),
      telegram: stubNamespace(["send", "reply"]),
      signal: stubNamespace(["send", "reply"]),
      imessage: stubNamespace(["send", "reply"]),
      whatsapp: stubNamespace(["send", "reply"]),
      line: stubNamespace(["send", "reply"]),
    },
  };
}
