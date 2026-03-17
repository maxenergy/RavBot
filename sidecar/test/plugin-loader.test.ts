// Copyright 2024 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { HookDispatcher } from "../src/hook-dispatcher.js";
import { ToolExecutor } from "../src/tool-executor.js";
import { createPluginRuntime } from "../src/plugin-runtime-shim.js";
import { loadPlugins } from "../src/plugin-loader.js";
import { createPluginApi, createPluginRecord, type PluginRegistries } from "../src/plugin-api-shim.js";
import type { PluginLogger, SidecarStartupConfig } from "../src/types.js";

function createTestLogger(): PluginLogger & { messages: string[] } {
  const messages: string[] = [];
  return {
    messages,
    debug: (msg: string) => messages.push(`debug: ${msg}`),
    info: (msg: string) => messages.push(`info: ${msg}`),
    warn: (msg: string) => messages.push(`warn: ${msg}`),
    error: (msg: string) => messages.push(`error: ${msg}`),
  };
}

function createTestRegistries(): PluginRegistries {
  return {
    tools: new ToolExecutor(),
    hooks: new HookDispatcher(createTestLogger()),
    httpHandlers: [],
    httpRoutes: [],
    channels: [],
    services: [],
    providers: [],
    commands: [],
    gatewayMethods: [],
    cliEntries: [],
  };
}

describe("PluginApiShim", () => {
  let logger: ReturnType<typeof createTestLogger>;
  let registries: PluginRegistries;

  beforeEach(() => {
    logger = createTestLogger();
    registries = createTestRegistries();
  });

  it("should register tools", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerTool({
      name: "my_tool",
      description: "A test tool",
      inputSchema: { type: "object", properties: { x: { type: "number" } } },
      run: (params) => ({ result: (params.x as number) * 2 }),
    });

    expect(record.toolNames).toEqual(["my_tool"]);
    expect(registries.tools.has("my_tool")).toBe(true);
  });

  it("should register tool factories", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerTool((_ctx) => [
      { name: "tool_a", description: "Tool A", run: () => "a" },
      { name: "tool_b", description: "Tool B", run: () => "b" },
    ]);

    expect(record.toolNames).toEqual(["tool_a", "tool_b"]);
    expect(registries.tools.toolNames()).toEqual(["tool_a", "tool_b"]);
  });

  it("should register hooks via registerHook", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerHook("message_received", async () => {});
    api.registerHook(
      ["llm_input", "llm_output"],
      async () => {},
      { priority: 10 },
    );

    expect(record.hookNames).toEqual(["message_received", "llm_input", "llm_output"]);
    expect(record.hookCount).toBe(3);
  });

  it("should register hooks via on()", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.on("before_model_resolve", async () => ({ model: "test" }));

    expect(record.hookNames).toEqual(["before_model_resolve"]);
    expect(record.hookCount).toBe(1);
  });

  it("should register HTTP handlers", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerHttpHandler(async () => null);
    api.registerHttpRoute({
      path: "/webhook",
      handler: async () => ({ status: 200 }),
    });

    expect(record.httpHandlerCount).toBe(2);
    expect(registries.httpHandlers.length).toBe(1);
    expect(registries.httpRoutes.length).toBe(1);
    expect(registries.httpRoutes[0].path).toBe("/plugins/test/webhook");
  });

  it("should register services", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerService({
      id: "bg-worker",
      start: async () => {},
      stop: async () => {},
    });

    expect(record.serviceIds).toEqual(["bg-worker"]);
    expect(registries.services.length).toBe(1);
  });

  it("should register providers", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerProvider({
      id: "my-provider",
      label: "My Provider",
      auth: [],
    });

    expect(record.providerIds).toEqual(["my-provider"]);
    expect(registries.providers.length).toBe(1);
  });

  it("should register commands", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerCommand({
      name: "status",
      description: "Show status",
      handler: () => ({ text: "OK" }),
    });

    expect(record.commandNames).toEqual(["status"]);
  });

  it("should register channels", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerChannel({
      plugin: { id: "telegram", meta: { name: "Telegram" } },
    });

    expect(record.channelIds).toEqual(["telegram"]);
    expect(registries.channels.length).toBe(1);
  });

  it("should register gateway methods", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    api.registerGatewayMethod("custom.method", async () => ({ ok: true }));

    expect(record.gatewayMethods).toEqual(["custom.method"]);
  });

  it("should resolve paths with ~ expansion", () => {
    const record = createPluginRecord({
      id: "test",
      name: "Test",
      source: "/test/index.ts",
    });

    const runtime = createPluginRuntime({ config: {}, logger });
    const api = createPluginApi({
      pluginId: "test",
      pluginName: "Test",
      source: "/test/index.ts",
      config: {},
      runtime,
      logger,
      record,
      registries,
    });

    const result = api.resolvePath("~/test");
    expect(result).toContain("/test");
    expect(result).not.toContain("~");
  });
});

describe("PluginLoader (filesystem)", () => {
  let tmpDir: string;
  let logger: ReturnType<typeof createTestLogger>;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "qc-loader-test-"));
    logger = createTestLogger();
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("should load a simple JavaScript plugin", async () => {
    // Create a plugin directory with manifest and entry point.
    const pluginDir = path.join(tmpDir, "plugins", "test-plugin");
    fs.mkdirSync(pluginDir, { recursive: true });

    fs.writeFileSync(
      path.join(pluginDir, "openclaw.plugin.json"),
      JSON.stringify({
        id: "test-plugin",
        name: "Test Plugin",
        version: "1.0.0",
        description: "A test plugin",
      }),
    );

    fs.writeFileSync(
      path.join(pluginDir, "index.js"),
      `
      export default {
        id: "test-plugin",
        name: "Test Plugin",
        register(api) {
          api.registerTool({
            name: "test_tool",
            description: "A test tool",
            run: (params) => ({ echo: params }),
          });

          api.on("message_received", async (event) => {
            // no-op
          });
        }
      };
      `,
    );

    const registries = createTestRegistries();
    const runtime = createPluginRuntime({ config: {}, logger });

    // SDK shim path — use the source file for testing.
    const sdkShimPath = path.resolve("src/plugin-sdk-shim.ts");

    const startupConfig: SidecarStartupConfig = {
      enabled_plugins: ["test-plugin"],
      workspace_dir: tmpDir,
      plugins: {},
    };

    // Manually set HOME to tmpDir so discovery finds our plugin.
    const origHome = process.env.HOME;
    process.env.HOME = tmpDir;

    // Create plugin in workspace path.
    const wsPluginDir = path.join(tmpDir, ".openclaw", "plugins", "test-plugin");
    fs.mkdirSync(wsPluginDir, { recursive: true });
    fs.writeFileSync(
      path.join(wsPluginDir, "openclaw.plugin.json"),
      JSON.stringify({
        id: "test-plugin",
        name: "Test Plugin",
        version: "1.0.0",
      }),
    );
    fs.writeFileSync(
      path.join(wsPluginDir, "index.js"),
      `
      export default {
        id: "test-plugin",
        register(api) {
          api.registerTool({
            name: "test_tool",
            description: "A test tool",
            run: (params) => ({ echo: params }),
          });
          api.on("message_received", async () => {});
        }
      };
      `,
    );

    try {
      const result = await loadPlugins({
        startupConfig,
        runtime,
        logger,
        registries,
        sdkShimPath,
      });

      expect(result.records.length).toBe(1);
      expect(result.records[0].id).toBe("test-plugin");
      expect(result.records[0].toolNames).toContain("test_tool");
      expect(result.records[0].hookCount).toBeGreaterThan(0);
    } finally {
      process.env.HOME = origHome;
    }
  });
});

describe("ToolExecutor", () => {
  it("should register and execute tools", async () => {
    const executor = new ToolExecutor();

    executor.register("test-plugin", {
      name: "add",
      description: "Add two numbers",
      inputSchema: {
        type: "object",
        properties: {
          a: { type: "number" },
          b: { type: "number" },
        },
      },
      run: (params) => ({ sum: (params.a as number) + (params.b as number) }),
    });

    expect(executor.has("add")).toBe(true);
    expect(executor.toolNames()).toEqual(["add"]);

    const result = await executor.execute("add", { a: 2, b: 3 });
    expect(result).toEqual({ sum: 5 });
  });

  it("should throw for unknown tool", async () => {
    const executor = new ToolExecutor();
    await expect(executor.execute("unknown", {})).rejects.toThrow("Tool not found");
  });

  it("should return schemas", () => {
    const executor = new ToolExecutor();
    executor.register("p1", {
      name: "tool1",
      description: "Desc 1",
      inputSchema: { type: "object" },
      run: () => ({}),
    });
    executor.register("p2", {
      name: "tool2",
      description: "Desc 2",
      run: () => ({}),
    });

    const schemas = executor.getSchemas();
    expect(schemas.length).toBe(2);
    expect(schemas[0].name).toBe("tool1");
    expect(schemas[1].name).toBe("tool2");
  });

  it("should track tools per plugin", () => {
    const executor = new ToolExecutor();
    executor.register("p1", { name: "a", description: "", run: () => ({}) });
    executor.register("p1", { name: "b", description: "", run: () => ({}) });
    executor.register("p2", { name: "c", description: "", run: () => ({}) });

    expect(executor.toolsForPlugin("p1")).toEqual(["a", "b"]);
    expect(executor.toolsForPlugin("p2")).toEqual(["c"]);
  });
});
