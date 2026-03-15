# Embedding 配置指南

## 概述

QuantClaw 支持通过 JSON 配置文件管理多个 embedding providers，支持环境变量替换和灵活的配置选项。

## 配置文件格式

```json
{
  "embedding": {
    "default_provider": "local",
    "providers": {
      "provider_name": {
        "type": "provider_type",
        // provider-specific options
      }
    }
  }
}
```

## 支持的 Provider 类型

### 1. Mock Provider (测试用)

```json
{
  "mock": {
    "type": "mock",
    "dimension": 384
  }
}
```

**参数：**
- `dimension`: 向量维度（默认：384）

**用途：** 单元测试和开发

### 2. Local Provider (本地 TF-IDF + LSA)

```json
{
  "local": {
    "type": "local",
    "dimension": 384,
    "corpus_file": "/path/to/corpus.txt"
  }
}
```

**参数：**
- `dimension`: 向量维度（默认：384）
- `corpus_file`: 可选，训练语料库文件路径（每行一个文档）

**特点：**
- 完全本地化，无需网络
- 支持自定义语料库训练
- 毫秒级响应
- 完全隐私保护

### 3. OpenAI Provider

```json
{
  "openai": {
    "type": "openai",
    "api_key": "${OPENAI_API_KEY}",
    "model": "text-embedding-3-small",
    "batch_size": 100,
    "endpoint": "https://api.openai.com/v1/embeddings"
  }
}
```

**参数：**
- `api_key`: OpenAI API 密钥（支持环境变量）
- `model`: 模型名称
  - `text-embedding-3-small` (1536维)
  - `text-embedding-3-large` (3072维)
  - `text-embedding-ada-002` (1536维)
- `batch_size`: 批量大小（默认：100，最大：2048）
- `endpoint`: API 端点（可选）

### 4. Anthropic Provider (Voyage AI)

```json
{
  "anthropic": {
    "type": "anthropic",
    "api_key": "${VOYAGE_API_KEY}",
    "model": "voyage-3",
    "batch_size": 128,
    "input_type": "document",
    "endpoint": "https://api.voyageai.com/v1/embeddings"
  }
}
```

**参数：**
- `api_key`: Voyage AI API 密钥（支持环境变量）
- `model`: 模型名称
  - `voyage-3` (1024维)
  - `voyage-3-lite` (512维)
  - `voyage-code-3` (1024维)
  - `voyage-2` (1024维)
  - `voyage-large-2` (1536维)
  - `voyage-code-2` (1536维)
- `batch_size`: 批量大小（默认：128，最大：128）
- `input_type`: 输入类型（`query` 或 `document`）
- `endpoint`: API 端点（可选）

## 环境变量

配置文件支持 `${VAR_NAME}` 格式的环境变量替换：

```json
{
  "api_key": "${OPENAI_API_KEY}",
  "corpus_file": "${HOME}/corpus/documents.txt"
}
```

**常用环境变量：**
- `OPENAI_API_KEY`: OpenAI API 密钥
- `VOYAGE_API_KEY`: Voyage AI API 密钥
- `CORPUS_FILE`: 训练语料库文件路径

## 使用示例

### C++ 代码

```cpp
#include "quantclaw/core/embedding_config.hpp"
#include "quantclaw/core/embedding_manager.hpp"
#include "quantclaw/core/vector_database.hpp"

// 从配置文件加载
auto registry = EmbeddingConfig::LoadFromFile("config/embedding.json");

// 创建 vector database
auto db = std::make_shared<VectorDatabase>(":memory:", logger);
db->Initialize();

// 创建 embedding manager
auto manager = std::make_shared<EmbeddingManager>(registry, db);

// 使用默认 provider
manager->IndexText("doc1", "Machine learning is great");

// 切换 provider
manager->SetProvider("openai");
manager->IndexText("doc2", "Deep learning uses neural networks");

// 搜索
auto results = manager->SearchText("artificial intelligence", 10);
```

### 从 JSON 对象加载

```cpp
nlohmann::json config = {
  {"embedding", {
    {"default_provider", "local"},
    {"providers", {
      {"local", {
        {"type", "local"},
        {"dimension", 384}
      }}
    }}
  }}
};

auto registry = EmbeddingConfig::LoadFromJson(config);
```

## 完整配置示例

参见 `config/embedding.example.json`：

```json
{
  "embedding": {
    "default_provider": "local",
    "providers": {
      "mock": {
        "type": "mock",
        "dimension": 384
      },
      "local": {
        "type": "local",
        "dimension": 384,
        "corpus_file": "${CORPUS_FILE}"
      },
      "openai": {
        "type": "openai",
        "api_key": "${OPENAI_API_KEY}",
        "model": "text-embedding-3-small",
        "batch_size": 100
      },
      "anthropic": {
        "type": "anthropic",
        "api_key": "${VOYAGE_API_KEY}",
        "model": "voyage-3",
        "batch_size": 128,
        "input_type": "document"
      }
    }
  }
}
```

## 最佳实践

### 1. 开发环境

```json
{
  "embedding": {
    "default_provider": "mock",
    "providers": {
      "mock": {
        "type": "mock",
        "dimension": 384
      }
    }
  }
}
```

**优点：**
- 快速启动
- 无需 API 密钥
- 确定性输出

### 2. 本地部署

```json
{
  "embedding": {
    "default_provider": "local",
    "providers": {
      "local": {
        "type": "local",
        "dimension": 384,
        "corpus_file": "/data/corpus.txt"
      }
    }
  }
}
```

**优点：**
- 完全离线
- 隐私保护
- 低延迟

### 3. 生产环境

```json
{
  "embedding": {
    "default_provider": "openai",
    "providers": {
      "local": {
        "type": "local",
        "dimension": 384
      },
      "openai": {
        "type": "openai",
        "api_key": "${OPENAI_API_KEY}",
        "model": "text-embedding-3-small",
        "batch_size": 100
      }
    }
  }
}
```

**优点：**
- 高质量嵌入
- 本地 fallback
- 灵活切换

## 故障排查

### API 密钥未设置

**错误：** `OpenAI API key is empty`

**解决：**
```bash
export OPENAI_API_KEY="your-api-key"
```

### 配置文件未找到

**错误：** `Failed to open config file`

**解决：**
- 检查文件路径
- 确保文件存在
- 检查文件权限

### Provider 未注册

**错误：** `Provider not found: xxx`

**解决：**
- 检查配置文件中的 provider 名称
- 确保 provider 已正确配置
- 检查 provider 类型是否支持

## 性能调优

### 批量大小

```json
{
  "openai": {
    "batch_size": 100  // 增大以提高吞吐量
  }
}
```

**建议：**
- OpenAI: 50-200
- Anthropic: 64-128
- 根据网络延迟调整

### 缓存配置

```cpp
manager->SetCacheEnabled(true);
manager->SetMaxCacheSize(10000);  // 调整缓存大小
```

### Provider 选择

| 场景 | 推荐 Provider | 原因 |
|------|--------------|------|
| 开发测试 | Mock | 快速、确定性 |
| 离线部署 | Local | 无需网络 |
| 高质量搜索 | OpenAI | 语义理解好 |
| 代码搜索 | Anthropic (voyage-code-3) | 专门优化 |
| 成本敏感 | Local | 免费 |

## 安全建议

1. **不要在配置文件中硬编码 API 密钥**
   ```json
   // ❌ 错误
   "api_key": "sk-abc123..."

   // ✅ 正确
   "api_key": "${OPENAI_API_KEY}"
   ```

2. **使用环境变量管理敏感信息**
   ```bash
   export OPENAI_API_KEY="sk-..."
   export VOYAGE_API_KEY="pa-..."
   ```

3. **限制配置文件权限**
   ```bash
   chmod 600 config/embedding.json
   ```

4. **定期轮换 API 密钥**

## 参考资料

- [OpenAI Embeddings API](https://platform.openai.com/docs/guides/embeddings)
- [Voyage AI Documentation](https://docs.voyageai.com/)
- [QuantClaw Embedding Provider API](../include/quantclaw/core/embedding_provider.hpp)
