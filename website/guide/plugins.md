# Plugin Development Guide

Learn how to create custom skills and extend RavBot with your own functionality.

## Overview

RavBot's plugin system allows you to:
- Create custom **skills** (tools available to the agent)
- Implement **hooks** for lifecycle events
- Build **channel adapters** for new chat platforms
- Extend core **functionality**

Plugins run in a Node.js sidecar process with IPC communication to the main C++ agent.

## Creating Your First Plugin

### Plugin Structure

```
my-plugin/
├── manifest.json          # Plugin metadata
├── src/
│   ├── index.ts          # Main entry point
│   ├── skills/
│   │   └── my-skill.ts   # Skill implementation
│   └── hooks/
│       └── lifecycle.ts  # Hook handlers
├── package.json          # Dependencies
└── README.md            # Documentation
```

### Manifest (manifest.json)

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "A custom plugin for RavBot",
  "author": "Your Name",
  "engine": "ravbot@1.0.0",
  "entrypoint": "dist/index.js",
  "skills": ["my-skill", "another-skill"],
  "hooks": {
    "before_agent_init": "onBeforeAgentInit",
    "after_agent_init": "onAfterAgentInit",
    "on_message_received": "onMessageReceived",
    "on_tool_execute": "onToolExecute",
    "on_tool_result": "onToolResult",
    "on_error": "onError"
  }
}
```

### Basic Plugin (TypeScript)

```typescript
// src/index.ts
import type { RavBotPlugin, SkillContext, ToolDefinition } from '@ravbot/sdk'

export default class MyPlugin implements RavBotPlugin {
  async initialize(context: PluginContext) {
    console.log('MyPlugin initialized')
  }

  // Define your skills
  getSkills(): ToolDefinition[] {
    return [
      {
        name: 'my_skill',
        description: 'A custom skill that does something awesome',
        input_schema: {
          type: 'object',
          properties: {
            input: {
              type: 'string',
              description: 'Input parameter'
            }
          },
          required: ['input']
        }
      }
    ]
  }

  // Implement skill execution
  async executeSkill(skillName: string, params: any): Promise<string> {
    if (skillName === 'my_skill') {
      return `Processed: ${params.input}`
    }
    throw new Error(`Unknown skill: ${skillName}`)
  }

  // Hook handlers
  async onBeforeAgentInit(context: SkillContext) {
    console.log('Agent initializing...')
  }

  async onMessageReceived(context: SkillContext, message: any) {
    console.log('Message received:', message)
  }

  async onToolExecute(context: SkillContext, tool: any) {
    console.log('Tool executing:', tool.name)
  }
}
```

## Building Skills

### Skill Definition

A skill is a reusable piece of functionality:

```typescript
interface ToolDefinition {
  name: string                          // Unique identifier
  description: string                   // Human description
  input_schema: JSONSchema              // Parameter schema
}
```

### Example: Weather Skill

```typescript
async getSkills(): Promise<ToolDefinition[]> {
  return [
    {
      name: 'get_weather',
      description: 'Get current weather for a location',
      input_schema: {
        type: 'object',
        properties: {
          location: {
            type: 'string',
            description: 'City name or coordinates'
          },
          units: {
            type: 'string',
            enum: ['metric', 'imperial'],
            description: 'Temperature units'
          }
        },
        required: ['location']
      }
    }
  ]
}

async executeSkill(skillName: string, params: any): Promise<string> {
  if (skillName === 'get_weather') {
    const { location, units = 'metric' } = params

    // Call external API
    const response = await fetch(
      `https://api.weather.api/current?location=${location}&units=${units}`
    )
    const data = await response.json()

    return JSON.stringify(data)
  }

  throw new Error(`Unknown skill: ${skillName}`)
}
```

### Example: Database Query Skill

```typescript
async executeSkill(skillName: string, params: any): Promise<string> {
  if (skillName === 'query_db') {
    const { query, limit = 10 } = params

    // Validate query (prevent injection)
    if (!isValidQuery(query)) {
      throw new Error('Invalid query')
    }

    const connection = await this.db.getConnection()
    try {
      const results = await connection.query(query, { limit })
      return JSON.stringify(results)
    } finally {
      await connection.release()
    }
  }
}

private isValidQuery(query: string): boolean {
  // Implement query validation
  return !query.includes('DROP') && !query.includes('DELETE')
}
```

## Implementing Hooks

Hooks allow you to react to agent events:

### Available Hooks

| Hook | Fired When | Purpose |
|------|-----------|---------|
| `before_agent_init` | Agent starting | Setup resources |
| `after_agent_init` | Agent ready | Initialize connections |
| `on_message_received` | User message arrives | Pre-process input |
| `on_tool_execute` | Tool running | Monitor execution |
| `on_tool_result` | Tool completes | Process results |
| `on_error` | Error occurs | Handle failures |
| `before_shutdown` | Agent stopping | Cleanup |

### Hook Handler Example

```typescript
async onToolResult(context: SkillContext, result: any) {
  // Log tool results
  if (result.success) {
    console.log(`Tool ${result.name} succeeded in ${result.duration}ms`)

    // Send to monitoring
    await this.metrics.recordToolExecution({
      name: result.name,
      duration: result.duration,
      status: 'success'
    })
  } else {
    console.error(`Tool ${result.name} failed:`, result.error)

    // Alert on failures
    if (result.retryable) {
      // Notify retry system
    }
  }
}
```

## Working with Context

The `SkillContext` provides access to:

```typescript
interface SkillContext {
  agent: {
    id: string
    config: AgentConfig
    models: ModelRegistry
  }
  user: {
    id: string
    workspace: string
  }
  session: {
    id: string
    messages: Message[]
  }
  memory: {
    search(query: string): Promise<string>
    get(key: string): Promise<string>
    set(key: string, value: string): Promise<void>
  }
  files: {
    read(path: string): Promise<string>
    write(path: string, content: string): Promise<void>
    list(dir: string): Promise<string[]>
  }
  logger: Logger
  fetch(url: string, options?: any): Promise<Response>
}
```

### Using Context

```typescript
async onMessageReceived(context: SkillContext, message: any) {
  // Access user information
  console.log(`User ${context.user.id} sent message`)

  // Store data in memory
  await context.memory.set('last_user_message', message.content)

  // Search agent memory
  const relevant = await context.memory.search(message.content)
  console.log('Found relevant context:', relevant)

  // Access workspace files
  const config = await context.files.read('config.json')
  console.log('Agent config:', config)
}
```

## Plugin Development Workflow

### 1. Create Plugin Project

```bash
ravbot skill create my-plugin
cd my-plugin
```

### 2. Install Dependencies

```bash
npm install @ravbot/sdk
npm install --save-dev typescript ts-node
```

### 3. Implement Plugin

Edit `src/index.ts` with your skills and hooks.

### 4. Build Plugin

```bash
npm run build  # Compiles TypeScript to JavaScript
```

### 5. Install Locally

```bash
ravbot skill install ./my-plugin

# Or link for development
ravbot skill link ./my-plugin
```

### 6. Test Plugin

```bash
# Check plugin status
ravbot skill status

# Test skill execution
ravbot run "Use my_skill with input: hello"

# Check logs
ravbot logs tail | grep "my-plugin"
```

### 7. Publish Plugin

```bash
npm publish

# Register with RavBot hub (future)
ravbot skill publish
```

## Best Practices

### Error Handling
```typescript
async executeSkill(skillName: string, params: any): Promise<string> {
  try {
    // Validate inputs
    if (!params.required_field) {
      throw new Error('Missing required_field parameter')
    }

    // Perform operation
    const result = await this.doSomething(params)
    return JSON.stringify(result)
  } catch (error) {
    // Log error
    this.context.logger.error(`Skill failed: ${error.message}`)

    // Return user-friendly error
    throw new Error(`Failed to execute: ${error.message}`)
  }
}
```

### Input Validation
```typescript
private validateParams(params: any, schema: JSONSchema): boolean {
  // Use JSON schema validator
  const validator = ajv.compile(schema)
  return validator(params)
}

async executeSkill(skillName: string, params: any): Promise<string> {
  const skill = this.skills.find(s => s.name === skillName)

  if (!this.validateParams(params, skill.input_schema)) {
    throw new Error('Invalid parameters')
  }

  // Safe to use params now
  return await this.executeOperation(params)
}
```

### Resource Cleanup
```typescript
async onBeforeShutdown(context: SkillContext) {
  // Close database connections
  if (this.db) {
    await this.db.close()
  }

  // Stop background jobs
  if (this.backgroundJob) {
    this.backgroundJob.stop()
  }

  // Clean temporary files
  await this.cleanupTempFiles()
}
```

### Logging
```typescript
async executeSkill(skillName: string, params: any): Promise<string> {
  const startTime = Date.now()

  try {
    this.context.logger.info(`Executing skill: ${skillName}`)
    const result = await this.doSomething(params)

    const duration = Date.now() - startTime
    this.context.logger.info(`Skill completed in ${duration}ms`)

    return JSON.stringify(result)
  } catch (error) {
    this.context.logger.error(`Skill failed: ${error.message}`)
    throw error
  }
}
```

## Example: Complete Plugin

See the example plugins in the RavBot repository:
- `skills/weather/` - Weather skill
- `skills/github/` - GitHub integration
- `skills/healthcheck/` - System health monitoring

## Debugging

### Enable Debug Logging
```bash
export RAVBOT_LOG_LEVEL=debug
ravbot agent --id=main
```

### View Plugin Logs
```bash
ravbot logs tail --plugin=my-plugin
```

### Test Skill Directly
```bash
# In a Node.js REPL with plugin loaded
const plugin = require('./dist/index.js')
const instance = new plugin.default()
await instance.executeSkill('my_skill', { input: 'test' })
```

---

**Next**: [Learn about CLI](/guide/cli-reference) or [build from source](/guide/building).
