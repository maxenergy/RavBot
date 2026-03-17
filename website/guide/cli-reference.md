# CLI Reference

Complete command reference for RavBot.

## Global Options

```bash
ravbot [OPTIONS] COMMAND [ARGS]
```

- `--help, -h` — Show help message
- `--version, -v` — Show version
- `--config PATH` — Configuration file path
- `--log-level LEVEL` — Log level: `trace`, `debug`, `info`, `warn`, `error`

## Commands

### agent

Send a message to the agent.

```bash
ravbot agent [OPTIONS] MESSAGE
```

**Options:**
- `--session SESSION` — Session key to use (default: auto-generated)

**Examples:**
```bash
# Send a message (creates a new session automatically)
ravbot agent "Hello, introduce yourself"

# Use a specific session key
ravbot agent --session my:project "What's the status?"
```

### run

Send a message to the agent (alias for `agent`).

```bash
ravbot run MESSAGE
```

### eval

One-shot prompt evaluation — no session history is created or used.

```bash
ravbot eval PROMPT
```

**Examples:**
```bash
ravbot eval "What is 2 + 2?"
ravbot eval "Generate a random UUID"
```

### gateway

Manage the RPC gateway.

```bash
ravbot gateway [SUBCOMMAND] [OPTIONS]
```

**Subcommands:**

#### gateway (no subcommand)
Run the gateway in the foreground.

```bash
ravbot gateway
```

#### gateway install
Install as a system service (systemd on Linux, launchd on macOS).

```bash
ravbot gateway install
```

#### gateway uninstall
Remove the system service.

```bash
ravbot gateway uninstall
```

#### gateway start / stop / restart
Control the background daemon.

```bash
ravbot gateway start
ravbot gateway stop
ravbot gateway restart
```

#### gateway status
Check whether the gateway daemon is running.

```bash
ravbot gateway status
```

#### gateway call
Call any RPC method directly.

```bash
ravbot gateway call METHOD [JSON_PARAMS]
```

**Examples:**
```bash
ravbot gateway call gateway.health
ravbot gateway call config.get '{"path":"llm.model"}'
```

### sessions

Manage conversation sessions.

```bash
ravbot sessions SUBCOMMAND [OPTIONS]
```

#### sessions list

```bash
ravbot sessions list
```

#### sessions history

```bash
ravbot sessions history SESSION_KEY
```

#### sessions delete

```bash
ravbot sessions delete SESSION_KEY
```

#### sessions reset

```bash
ravbot sessions reset SESSION_KEY
```

### config

Manage configuration.

```bash
ravbot config SUBCOMMAND [OPTIONS]
```

#### config get

```bash
ravbot config get                    # Full config
ravbot config get llm.model         # Specific value (dot-path)
```

#### config set

```bash
ravbot config set llm.model "anthropic/claude-sonnet-4-6"
```

#### config unset

```bash
ravbot config unset llm.temperature
```

#### config reload
Hot-reload config without restarting the gateway.

```bash
ravbot config reload
```

#### config validate

```bash
ravbot config validate
```

#### config schema

```bash
ravbot config schema
```

### skills

Manage skills.

```bash
ravbot skills SUBCOMMAND
```

#### skills list

```bash
ravbot skills list
```

#### skills install

Install a skill's dependencies.

```bash
ravbot skills install SKILL_NAME
```

### memory

Search and inspect agent memory.

```bash
ravbot memory SUBCOMMAND [OPTIONS]
```

#### memory search

```bash
ravbot memory search "query string"
ravbot memory search "recent events" --limit 10
```

**Options:**
- `--limit N` — Result limit (default: 5)

#### memory status

```bash
ravbot memory status
```

### cron

Manage scheduled tasks.

```bash
ravbot cron SUBCOMMAND
```

#### cron list

```bash
ravbot cron list
```

#### cron add

```bash
ravbot cron add NAME "0 9 * * *" "Send daily summary"
```

#### cron remove

```bash
ravbot cron remove TASK_ID
```

### health

Quick health check — confirms the gateway is reachable.

```bash
ravbot health
```

### status

Show connection and session counts.

```bash
ravbot status
```

### logs

Stream gateway logs.

```bash
ravbot logs
```

### doctor

Run a full diagnostic check.

```bash
ravbot doctor
```

### dashboard

Open the web dashboard in the browser.

```bash
ravbot dashboard
```

Opens `http://127.0.0.1:18801`.

### onboard

Interactive setup wizard.

```bash
ravbot onboard [OPTIONS]
```

**Options:**
- `--quick` — Quick setup with defaults (non-interactive)
- `--install-daemon` — Also install the gateway as a system service

**Examples:**
```bash
ravbot onboard                     # Interactive
ravbot onboard --quick             # Non-interactive
ravbot onboard --install-daemon    # Interactive + install daemon
```

## In-Conversation Message Commands

While chatting, prefix a message with a slash command to control the session:

| Command | Effect |
|---------|--------|
| `/new` | Start a new session |
| `/reset` | Clear current session history |
| `/compact` | Manually trigger context compaction |
| `/status` | Show current session and queue status |
| `/commands` | List all available slash commands |
| `/help` | Show help text |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENAI_API_KEY` | OpenAI / compatible provider API key |
| `ANTHROPIC_API_KEY` | Anthropic API key |
| `RAVBOT_LOG_LEVEL` | Log level override (`debug`, `info`, `warn`, `error`) |
| `RAVBOT_PORT` | Sidecar IPC port (internal use, set automatically) |

## Ports

| Service | Port | Purpose |
|---------|------|---------|
| WebSocket RPC Gateway | `18800` | Main gateway for client connections |
| HTTP REST API / Dashboard | `18801` | Web UI and REST API |

## Complete Workflow Example

```bash
# 1. Initial setup
ravbot onboard --quick

# 2. Start gateway in background
ravbot gateway start

# 3. Send a message
ravbot agent "Hello!"

# 4. View session history
ravbot sessions list
ravbot sessions history SESSION_KEY

# 5. Search memory
ravbot memory search "project notes"

# 6. Check status
ravbot health
ravbot status

# 7. Stream logs
ravbot logs
```

---

**Need help?** Run `ravbot --help` or `ravbot COMMAND --help`.
