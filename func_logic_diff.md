# RavBot vs OpenClaw `src/` Core Module Functional/Logic Alignment

## Scope and method

This report recursively reviews RavBot's `src/` core submodules against the
closest OpenClaw `src/` counterparts, focusing on runtime logic and effective
feature depth rather than naming alone.

Primary references sampled for this report:

- RavBot
  - `src/core/agent_loop.cpp`
  - `src/core/prompt_builder.cpp`
  - `src/gateway/gateway_server.cpp`
  - `src/session/session_manager.cpp`
  - `src/tools/tool_registry.cpp`
  - `src/providers/provider_registry.cpp`
  - `src/channels/telegram_channel.cpp`
  - `src/plugins/plugin_system.cpp`
  - `src/security/sandbox.cpp`
- OpenClaw
  - `src/agents/system-prompt.ts`
  - `src/agents/pi-embedded-runner/run.ts`
  - `src/agents/pi-embedded-helpers/turns.ts`
  - `src/agents/tools/web-search.ts`
  - `src/gateway/server-methods/chat.ts`
  - `src/sessions/transcript-events.ts`
  - `src/telegram/bot.ts`
  - `src/plugins/hook-runner-global.ts`
  - `src/plugins/tools.ts`
  - `src/security/external-content.ts`

Alignment levels used below:

- `High`: same functional responsibility and roughly comparable logic depth
- `Medium`: same responsibility exists, but RavBot is materially simpler
- `Low`: partial overlap only; OpenClaw capability is much broader/deeper
- `Missing`: no real RavBot counterpart

## Executive summary

RavBot is **not yet functionally aligned** with OpenClaw at the core-system
level. It has the beginnings of the same architecture:

- agent loop
- gateway RPC
- session transcripts
- tool registry
- provider abstraction
- channel adapters
- plugin sidecar
- basic sandboxing

But OpenClaw has moved significantly further in five areas:

1. prompt/context orchestration
2. agent runtime resilience and failover
3. channel delivery semantics and route persistence
4. plugin/control-plane depth
5. security and external-content hardening

The result is that RavBot often matches OpenClaw's API surface or nominal
module names, but not the same behavior under real multi-turn, multi-channel,
multi-tool, or degraded-provider conditions.

## Module alignment matrix

| RavBot module | OpenClaw counterpart | Alignment | Main gap |
| --- | --- | --- | --- |
| `src/core` | `src/agents`, `src/context-engine`, `src/memory` | Low | RavBot has a simpler prompt/turn loop and much less context, failover, memory, and recovery logic |
| `src/gateway` | `src/gateway`, `src/auto-reply` | Medium-Low | RPC exists, but OpenClaw gateway owns richer chat sanitization, abort, delivery routing, attachment handling, and transcript injection |
| `src/session` | `src/sessions` | Medium-Low | RavBot stores JSONL transcripts and key normalization, but lacks eventing, policy layers, override plumbing, and repair/maintenance flows |
| `src/tools` | `src/agents/tools`, `src/process`, `src/browser` | Medium | Core file/exec/web tools exist, but OpenClaw has more guarded, provider-aware, and specialized tool implementations |
| `src/providers` | `src/providers`, `src/agents/model-*` | Medium-Low | Provider abstraction exists, but OpenClaw has much stronger auth/profile/fallback/model resolution logic |
| `src/channels` | `src/telegram`, `src/slack`, `src/signal`, `src/line`, `src/channels` | Low | RavBot channel logic is thin; OpenClaw channels carry sequencing, policy, dedupe, route memory, and richer native UX |
| `src/plugins` | `src/plugins`, `src/plugin-sdk`, `src/acp` | Low | RavBot sidecar/plugin bridge exists, but OpenClaw has loader, registry, hooks, optional tool gating, ACP control plane |
| `src/security` | `src/security`, `src/secrets` | Low | RavBot has path/command checks and resource limits; OpenClaw adds audit, external-content wrapping, scanner, ACL, dangerous-tool policy |
| `src/web` | `src/web`, `src/browser`, `src/link-understanding` | Low | RavBot can search/fetch; OpenClaw has richer web-search providers, guarded fetch, browser-level handling, content hardening |
| `src/cli` | `src/cli`, `src/commands`, `src/daemon`, `src/wizard` | Low | RavBot CLI is comparatively small and operational; OpenClaw CLI is an admin/control surface |
| `src/mcp` | `src/acp`, `src/node-host`, `src/plugin-sdk` | Low | RavBot MCP support is narrower than OpenClaw's broader protocol/control-plane integration |
| `src/platform` | `src/process`, `src/infra` | Medium-Low | Basic process/session helpers exist, but OpenClaw has broader command queueing and runtime infrastructure |

## Per-module findings

### 1. `src/core` vs `src/agents` / `src/context-engine` / `src/memory`

**Alignment: Low**

What is aligned:

- RavBot has a central agent loop and prompt builder.
- It supports tool-call replay and multi-turn continuation.
- It has emerging memory/vector support and subagent hooks.

What is not aligned:

- RavBot prompt construction is still substantially thinner than OpenClaw.
  OpenClaw's `system-prompt.ts` encodes skills protocol, memory recall rules,
  channel/reply behavior, runtime metadata, ACP/tool summaries, sender trust,
  output tags, and operational guardrails. RavBot's prompt builder mainly
  concatenates SOUL/AGENTS/TOOLS/memory/runtime context.
- OpenClaw's `pi-embedded-runner/run.ts` contains mature retry/failover logic:
  auth profile rotation, cooldowns, overload pacing, context-window guards,
  usage normalization, compaction diagnostics, and provider-specific recovery.
  RavBot's loop is much less defensive.
- OpenClaw has explicit turn validators like
  `pi-embedded-helpers/turns.ts` for Anthropic/Gemini sequencing repair.
  RavBot has added targeted replay guards, but not a similarly broad
  provider-normalization layer.
- OpenClaw integrates context-engine and memory subsystems as first-class prompt
  inputs. RavBot memory is present but not yet comparable in orchestration.

Practical impact:

- RavBot is more likely to produce brittle behavior in long conversations,
  tool-heavy turns, provider edge cases, and channel-specific instructions.

### 2. `src/gateway` vs `src/gateway` / `src/auto-reply`

**Alignment: Medium-Low**

What is aligned:

- RavBot has a WebSocket gateway server with authentication state,
  connection presence, RPC handler registration, snapshot generation, and
  response/event dispatch.
- RPC endpoints for chat/session/tool-like operations exist.

What is not aligned:

- OpenClaw's `server-methods/chat.ts` does far more than request routing:
  message sanitization, attachment normalization, abort handling, transcript
  injection, route inheritance, external delivery decisions, history sizing, and
  message-channel normalization.
- OpenClaw gateway logic participates in a larger auto-reply system.
  RavBot currently treats gateway more as transport plus handler dispatch.
- OpenClaw has richer client capability negotiation and route-aware delivery.
  RavBot snapshots/auth are simpler and less policy-driven.

Practical impact:

- RavBot can serve chat requests, but gateway semantics are still much less
  robust for concurrent clients, interrupt/abort flows, and mixed internal vs
  external delivery.

### 3. `src/session` vs `src/sessions`

**Alignment: Medium-Low**

What is aligned:

- RavBot has OpenClaw-style session-key normalization (`agent:<id>:<rest>`).
- It stores session metadata and JSONL transcript messages.
- It preserves structured content blocks for text, tool use, and tool results.

What is not aligned:

- OpenClaw sessions are a broader policy layer, not just persistence:
  send policy, model overrides, level overrides, transcript event listeners,
  provenance, and surrounding gateway/session utilities.
- RavBot lacks clear equivalents for transcript update eventing,
  session-scoped policy mutation, and transcript repair/maintenance workflows.
- OpenClaw's surrounding session ecosystem is tightly integrated with gateway
  delivery and auto-reply. RavBot's session manager is mostly a local store.

Practical impact:

- RavBot transcripts exist, but the system around them is not yet rich enough
  for advanced routing, auditing, UI synchronization, or policy injection.

### 4. `src/tools` vs `src/agents/tools` / `src/process` / `src/browser`

**Alignment: Medium**

What is aligned:

- RavBot has the expected core agent tools: `read`, `write`, `edit`, `exec`,
  `bash`, `apply_patch`, `process`, `message`, `web_search`, `web_fetch`,
  `memory_search`, `memory_get`, `chain`, MCP-backed tools, and subagent hooks.
- This is materially closer to OpenClaw than several other modules.

What is not aligned:

- OpenClaw tool implementations are generally more policy-rich and environment-
  aware. Example: `agents/tools/web-search.ts` supports multiple providers,
  cache handling, transport-specific schemas, citation redirects, trusted web
  endpoint wrappers, and more nuanced filters.
- OpenClaw's tool space includes more browser/media/process/context capabilities
  than RavBot currently exposes.
- RavBot tool permissioning exists, but many tool handlers are still direct
  single-file implementations rather than deeply guarded workflows.

Practical impact:

- RavBot has decent baseline tool parity for coding-agent workflows, but not
  equivalent operational maturity or specialization.

### 5. `src/providers` vs `src/providers` / `src/agents/model-*`

**Alignment: Medium-Low**

What is aligned:

- RavBot has a provider registry, builtin provider factories, alias loading,
  base URL/API key resolution, and provider-per-model lookup.
- Major providers are covered through native or OpenAI-compatible adapters.

What is not aligned:

- OpenClaw splits provider concerns across auth, model selection, fallback
  ordering, profile cooldown, and provider-specific runtime policies.
- OpenClaw's embedded runner treats provider failure as a first-class runtime
  state machine. RavBot mostly treats providers as interchangeable transport
  backends.
- RavBot has fewer provider-specific behavioral patches and fewer recovery
  paths after malformed turns, auth issues, overload, or billing failures.

Practical impact:

- RavBot can talk to many models, but model reliability and failover behavior
  are not at OpenClaw's level.

### 6. `src/channels` vs `src/telegram` / other channel stacks

**Alignment: Low**

What is aligned:

- RavBot has Telegram channel support with polling, allowlist checks,
  message chunking, reply support, update offset persistence, and basic send/edit/delete.

What is not aligned:

- OpenClaw's `telegram/bot.ts` and related files implement a much broader system:
  update dedupe, safe watermark persistence, sequential lane processing, thread
  binding, chat-action handling, account resolution, command menus, history
  management, reply modes, DM/group policy, media flows, and native command
  integration.
- OpenClaw channels are part of a shared channel/routing policy layer. RavBot
  channels are more adapter-like and isolated.
- RavBot currently has much thinner native UX and fewer route semantics.

Practical impact:

- RavBot channel behavior works for basic inbound/outbound messaging, but not
  with OpenClaw's delivery guarantees, threading model, or channel-native depth.

### 7. `src/plugins` and `src/mcp` vs `src/plugins` / `src/plugin-sdk` / `src/acp`

**Alignment: Low**

What is aligned:

- RavBot has plugin discovery, sidecar startup, sidecar RPC calls, hook
  firing (`gateway_start` / `gateway_stop`), tool schema import, HTTP/CLI/service
  forwarding, and provider/command listing.
- MCP integration exists as another extension path.

What is not aligned:

- OpenClaw plugin infrastructure is significantly richer:
  manifest registry, loader, diagnostics, optional tool gating, name-conflict
  handling, global hook runner, config-state normalization, and extensive tests.
- OpenClaw ACP is a separate control plane with session mapping, persistent
  bindings, translator logic, manager/runtime control layers, and policy tests.
- RavBot sidecar integration is serviceable, but not a comparable control
  plane or plugin lifecycle system.

Practical impact:

- RavBot plugin extensibility exists, but OpenClaw is much closer to a
  platform with pluggable runtime governance.

### 8. `src/security` vs `src/security` / `src/secrets`

**Alignment: Low**

What is aligned:

- RavBot sandboxing checks allowed/denied paths, denied command patterns,
  path traversal, dangerous shell fragments, and resource limits on Linux.

What is not aligned:

- OpenClaw security is much broader than sandboxing:
  audit channels, audit tooling, dangerous-tool detection, temp path guard,
  Windows ACL handling, safe-regex utilities, mutable allowlist detectors, and
  skill scanning.
- `security/external-content.ts` shows explicit prompt-injection hardening for
  external content using boundary wrappers and marker sanitization. RavBot
  currently has no comparable generalized external-content security layer.
- RavBot security logic is mostly pre-execution validation. OpenClaw adds
  content-origin trust modeling and auditability.

Practical impact:

- RavBot's security posture is still tool-execution-centric, while OpenClaw
  defends more strongly against prompt injection, unsafe configuration, and
  operational drift.

### 9. `src/web` vs `src/web` / `src/browser` / `src/link-understanding`

**Alignment: Low**

- RavBot web capability is primarily tool-driven search/fetch.
- OpenClaw's web stack is broader and better integrated with safety and browser
  workflows.
- RavBot does not yet have an OpenClaw-equivalent browser layer, link
  understanding stack, or comparable trusted-web abstraction.

Practical impact:

- RavBot can retrieve web text, but OpenClaw is better prepared for richer
  browsing workflows and safer external content ingestion.

### 10. `src/cli` vs `src/cli` / `src/commands` / `src/daemon` / `src/wizard`

**Alignment: Low**

- RavBot exposes core operational CLI functionality.
- OpenClaw's CLI/commands surface is much broader: onboarding, auth choice,
  doctor/health flows, sandbox UX, gateway setup, daemon install, channel
  management, models listing, sessions/admin utilities, and setup wizard flows.

Practical impact:

- RavBot CLI is usable for developers/operators, but not yet comparable as a
  full lifecycle control interface.

## OpenClaw core subsystems with no meaningful RavBot equivalent

These are not just "weaker" in RavBot; they are mostly absent as dedicated
subsystems:

- `src/acp`
  - separate control plane, runtime manager, persistent bindings, translator
- `src/auto-reply`
  - channel-aware autonomous reply orchestration and templating
- `src/browser`
  - browser-level execution and content handling
- `src/context-engine`
  - explicit context engine initialization/resolution
- `src/cron`
  - broader scheduled automation stack
- `src/discord`, `src/slack`, `src/signal`, `src/line`, `src/whatsapp`
  - multi-channel depth beyond Telegram
- `src/media` / `src/media-understanding`
  - richer multimodal/media pipeline
- `src/routing`
  - explicit routing subsystem
- `src/secrets`
  - stronger secret and auth-profile ecosystem
- `src/tts`
  - text-to-speech subsystem
- `src/tui`
  - terminal UI layer
- `src/wizard`
  - interactive setup/admin workflows

## Highest-priority functional gaps to close

If the goal is "behavioral alignment" rather than directory parity, the most
valuable next steps are:

1. **Strengthen `src/core` prompt/runtime logic**
   - Bring RavBot prompt composition closer to OpenClaw's system-prompt model:
     skill protocol, memory recall policy, channel instructions, sender trust,
     output formatting constraints, and richer runtime/tool summaries.
   - Add provider-specific turn normalization and broader retry/failover logic.

2. **Upgrade gateway/session/channel integration**
   - Add route-aware delivery metadata, transcript update events, abort semantics,
     message sanitization, and stronger channel-specific session behavior.
   - Telegram should gain dedupe, sequencing, thread/topic handling, and safer
     update watermark management.

3. **Add an external-content security layer**
   - Web search/fetch and channel-ingested content should be wrapped and marked as
     untrusted before reaching the model, similar to OpenClaw's
     `security/external-content.ts`.

4. **Deepen plugin/control-plane architecture**
   - Expand sidecar/plugin lifecycle, conflict diagnostics, optional tool gating,
     hook coverage, and service/runtime management.
   - Decide whether RavBot will build an ACP-equivalent control plane or
     intentionally stay simpler.

5. **Improve provider resilience**
   - Add auth profile rotation, cooldown, model fallback ordering, context-window
     guards, and provider-specific error classification/recovery.

## Conclusion

RavBot currently aligns with OpenClaw at the **architectural outline** level,
not at the **logic-depth** level.

The strongest relative alignment today is:

- tool registry / coding-agent primitives
- basic session/transcript shape
- provider abstraction

The weakest alignment today is:

- prompt/context orchestration
- channel runtime semantics
- plugin/control plane
- security hardening
- operational/admin surfaces

If future work should prioritize user-visible reliability, the best order is:

1. core runtime + prompt alignment
2. gateway/session/channel behavior
3. external-content security
4. provider failover
5. plugin/control-plane depth
