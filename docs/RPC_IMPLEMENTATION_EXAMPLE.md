# RavBot RPC 方法实现示例

## 示例：models.list 实现

这是一个完整的 RPC 方法实现示例，展示了如何在 RavBot 中添加新的 RPC 方法。

### 1. 参考 OpenClaw 实现

**文件**: `/home/rogers/develop/openclaw/src/gateway/server-methods/models.ts`

```typescript
export const modelsHandlers: GatewayRequestHandlers = {
  "models.list": async ({ params, respond, context }) => {
    try {
      const catalog = await context.loadGatewayModelCatalog();
      const cfg = loadConfig();
      const { allowedCatalog } = buildAllowedModelSet({
        cfg,
        catalog,
        defaultProvider: DEFAULT_PROVIDER,
      });
      const models = allowedCatalog.length > 0 ? allowedCatalog : catalog;
      respond(true, { models }, undefined);
    } catch (err) {
      respond(false, undefined, errorShape(ErrorCodes.UNAVAILABLE, String(err)));
    }
  },
};
```

### 2. RavBot C++ 实现

**文件**: `/home/rogers/source/develop/RavBot/src/gateway/rpc_handlers.cpp`

```cpp
// --- models.list ---
// Return list of available models from all registered providers
server.RegisterHandler(methods::kOcModelsList,
    [provider_registry, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
        try {
            auto catalog = provider_registry->GetModelCatalog();
            nlohmann::json models = nlohmann::json::array();

            for (const auto& entry : catalog) {
                models.push_back(entry.ToJson());
            }

            logger->debug("models.list: returning {} models", models.size());
            return {{"models", models}};
        } catch (const std::exception& e) {
            logger->error("models.list failed: {}", e.what());
            throw;
        }
    }
);
```

### 3. 实现步骤

1. **查找 OpenClaw 实现**
   - 在 `/home/rogers/develop/openclaw/src/gateway/server-methods/` 中查找对应方法
   - 理解方法的输入参数和返回值

2. **检查 RavBot 基础设施**
   - 确认 ProviderRegistry 是否有 `GetModelCatalog()` 方法
   - 确认 protocol.hpp 中是否定义了方法常量（如 `kOcModelsList`）

3. **实现 RPC 处理器**
   - 在 `register_rpc_handlers()` 函数中添加新的处理器
   - 使用 `server.RegisterHandler()` 注册方法
   - 使用 lambda 函数实现处理逻辑

4. **错误处理**
   - 使用 try-catch 捕获异常
   - 记录错误日志
   - 返回适当的错误响应

5. **编译验证**
   ```bash
   cd /home/rogers/source/develop/RavBot/build
   make -j$(nproc)
   ```

6. **更新处理器计数**
   - 在文件末尾更新 `handler_count` 变量

### 4. 关键点

- **Lambda 捕获**: 使用 `[provider_registry, logger]` 捕获需要的依赖
- **参数解析**: 使用 `params.value("key", default)` 安全获取参数
- **返回格式**: 返回 `nlohmann::json` 对象，与 OpenClaw 协议兼容
- **日志记录**: 使用 `logger->debug/info/error` 记录关键信息
- **异常处理**: 捕获并记录异常，避免崩溃

### 5. 测试

```bash
# 启动 gateway
./ravbot gateway

# 测试 RPC 方法
curl -X POST http://localhost:18801/api/rpc \
  -H "Content-Type: application/json" \
  -d '{"method": "models.list", "params": {}, "id": 1}'
```

### 6. 已实现的新 RPC 方法

#### 6.1 skills.status
返回所有已加载技能的状态信息。

```cpp
server.RegisterHandler(methods::kOcSkillsStatus,
    [&config, skill_loader, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
        try {
            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            auto workspace_path = std::filesystem::path(home_str) / ".ravbot/agents/main/workspace";

            auto skills = skill_loader->LoadSkills(config.skills, workspace_path);
            nlohmann::json skills_array = nlohmann::json::array();

            for (const auto& skill : skills) {
                nlohmann::json skill_entry = nlohmann::json{
                    {"name", skill.name},
                    {"emoji", skill.emoji},
                    {"description", skill.description},
                    {"always", skill.always},
                    {"available", true}
                };
                skills_array.push_back(skill_entry);
            }

            logger->debug("skills.status: returning {} skills", skills_array.size());
            return {
                {"skills", skills_array},
                {"count", skills_array.size()}
            };
        } catch (const std::exception& e) {
            logger->error("skills.status failed: {}", e.what());
            throw;
        }
    }
);
```

#### 6.2 skills.bins
返回所有技能所需的二进制依赖列表。

```cpp
server.RegisterHandler(methods::kOcSkillsBins,
    [&config, skill_loader, logger](const nlohmann::json& /*params*/, ClientConnection& /*client*/) -> nlohmann::json {
        try {
            const char* home = std::getenv("HOME");
            std::string home_str = home ? home : "/tmp";
            auto workspace_path = std::filesystem::path(home_str) / ".ravbot/agents/main/workspace";

            auto skills = skill_loader->LoadSkills(config.skills, workspace_path);
            std::set<std::string> bins_set;

            for (const auto& skill : skills) {
                for (const auto& bin : skill.required_bins) {
                    if (!bin.empty()) {
                        bins_set.insert(bin);
                    }
                }
            }

            nlohmann::json bins = nlohmann::json::array();
            for (const auto& bin : bins_set) {
                bins.push_back(bin);
            }

            logger->debug("skills.bins: returning {} bins", bins.size());
            return {{"bins", bins}};
        } catch (const std::exception& e) {
            logger->error("skills.bins failed: {}", e.what());
            throw;
        }
    }
);
```

#### 6.3 secrets.resolve
解析密钥（简化实现，从环境变量读取）。

```cpp
server.RegisterHandler(methods::kOcSecretsResolve,
    [logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
        try {
            std::string key = params.value("key", "");
            if (key.empty()) {
                throw std::runtime_error("Missing 'key' parameter");
            }

            // Try to resolve from environment variable
            const char* value = std::getenv(key.c_str());
            if (value) {
                logger->debug("secrets.resolve: resolved key '{}'", key);
                return {
                    {"key", key},
                    {"value", value},
                    {"source", "env"}
                };
            }

            logger->warn("secrets.resolve: key '{}' not found", key);
            return {
                {"key", key},
                {"value", nlohmann::json(nullptr)},
                {"source", "none"}
            };
        } catch (const std::exception& e) {
            logger->error("secrets.resolve failed: {}", e.what());
            throw;
        }
    }
);
```

#### 6.4 logs.tail
返回日志文件的最后 N 行。

```cpp
server.RegisterHandler(methods::kOcLogsTail,
    [log_file_path, logger](const nlohmann::json& params, ClientConnection& /*client*/) -> nlohmann::json {
        try {
            int lines = params.value("lines", 100);
            if (lines <= 0) lines = 100;
            if (lines > 10000) lines = 10000;  // Safety limit

            std::vector<std::string> log_lines;
            std::ifstream log_file(log_file_path);
            if (log_file.is_open()) {
                std::string line;
                while (std::getline(log_file, line)) {
                    log_lines.push_back(line);
                }
                log_file.close();
            }

            // Return last N lines
            int start = std::max(0, static_cast<int>(log_lines.size()) - lines);
            nlohmann::json result = nlohmann::json::array();
            for (int i = start; i < static_cast<int>(log_lines.size()); ++i) {
                result.push_back(log_lines[i]);
            }

            logger->debug("logs.tail: returning {} lines", result.size());
            return {
                {"lines", result},
                {"total", log_lines.size()}
            };
        } catch (const std::exception& e) {
            logger->error("logs.tail failed: {}", e.what());
            throw;
        }
    }
);
```

### 7. 其他待实现的 RPC 方法

使用相同的模式实现以下方法：

**设备配对相关 (6 个)**
- `device:pair:list` - 列出设备配对请求
- `device:pair:approve` - 批准设备配对
- `device:pair:reject` - 拒绝设备配对
- `device:pair:remove` - 移除设备配对
- `device:token:rotate` - 轮换设备令牌
- `device:token:revoke` - 撤销设备令牌

**执行批准相关 (6 个)**
- `exec:approvals:get` - 获取执行批准策略
- `exec:approvals:set` - 设置执行批准策略
- `exec:approval:request` - 请求执行批准
- `exec:approval:resolve` - 解决执行批准
- `exec:approvals:node:get` - 获取节点执行批准
- `exec:approvals:node:set` - 设置节点执行批准

**节点操作相关 (11 个)**
- `node:pair:request` - 请求节点配对
- `node:pair:list` - 列出节点配对
- `node:pair:approve` - 批准节点配对
- `node:pair:reject` - 拒绝节点配对
- `node:pair:verify` - 验证节点配对
- `node:rename` - 重命名节点
- `node:list` - 列出节点
- `node:describe` - 描述节点
- `node:invoke` - 调用节点
- `node:invoke:result` - 获取节点调用结果
- `node:event` - 节点事件

每个方法都遵循相同的实现模式：
1. 参考 OpenClaw 实现
2. 在 rpc_handlers.cpp 中添加处理器
3. 编译验证
4. 测试功能

### 8. 实现进度

**已完成 (66/79 方法)**:
- ✅ models.list (已存在)
- ✅ skills.status (新增)
- ✅ skills.bins (新增)
- ✅ secrets.resolve (新增)
- ✅ logs.tail (新增)
- ✅ tools.catalog (已存在)

**待实现 (23 个方法)**:
- ❌ 设备配对相关 (6 个)
- ❌ 执行批准相关 (6 个)
- ❌ 节点操作相关 (11 个)
