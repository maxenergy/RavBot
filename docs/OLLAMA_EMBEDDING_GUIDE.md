# Ollama Embedding Provider 使用指南

## 概述

OllamaEmbeddingProvider 允许 RavBot 使用本地运行的 Ollama 服务进行文本嵌入。Ollama 是一个轻量级的本地 LLM 运行工具，支持多种开源嵌入模型。

## 优势

- ✅ **完全本地化** - 数据不离开本地
- ✅ **隐私保护** - 无需 API 密钥
- ✅ **免费使用** - 无使用成本
- ✅ **高质量** - 支持多种优秀的开源模型
- ✅ **低延迟** - 本地推理，无网络延迟
- ✅ **离线可用** - 无需互联网连接

## 安装 Ollama

### Linux
```bash
curl -fsSL https://ollama.com/install.sh | sh
```

### macOS
```bash
brew install ollama
```

### Windows
下载安装程序：https://ollama.com/download

## 支持的嵌入模型

### 1. nomic-embed-text (推荐)
```bash
ollama pull nomic-embed-text
```
- **维度**: 768
- **特点**: 高质量通用嵌入
- **适用**: 文本搜索、语义相似度

### 2. mxbai-embed-large
```bash
ollama pull mxbai-embed-large
```
- **维度**: 1024
- **特点**: 更大的模型，更好的性能
- **适用**: 高精度搜索

### 3. all-minilm
```bash
ollama pull all-minilm
```
- **维度**: 384
- **特点**: 轻量级，快速
- **适用**: 资源受限环境

## 配置

### 1. 基本配置

```json
{
  "embedding": {
    "default_provider": "ollama",
    "providers": {
      "ollama": {
        "type": "ollama",
        "model": "nomic-embed-text",
        "endpoint": "http://localhost:11434",
        "timeout": 30
      }
    }
  }
}
```

### 2. 多模型配置

```json
{
  "embedding": {
    "default_provider": "ollama_nomic",
    "providers": {
      "ollama_nomic": {
        "type": "ollama",
        "model": "nomic-embed-text",
        "endpoint": "http://localhost:11434"
      },
      "ollama_mxbai": {
        "type": "ollama",
        "model": "mxbai-embed-large",
        "endpoint": "http://localhost:11434"
      },
      "ollama_minilm": {
        "type": "ollama",
        "model": "all-minilm",
        "endpoint": "http://localhost:11434"
      }
    }
  }
}
```

### 3. 远程 Ollama 服务

```json
{
  "ollama": {
    "type": "ollama",
    "model": "nomic-embed-text",
    "endpoint": "http://192.168.1.100:11434"
  }
}
```

## 使用示例

### C++ 代码

```cpp
#include "ravbot/core/ollama_embedding_provider.hpp"
#include "ravbot/core/embedding_config.hpp"

// 方法 1: 直接创建
auto provider = std::make_shared<OllamaEmbeddingProvider>(
    "nomic-embed-text",
    "http://localhost:11434"
);

// 生成嵌入
auto embedding = provider->Embed("Hello, world!");
std::cout << "Dimension: " << embedding.size() << std::endl;

// 批量嵌入
std::vector<std::string> texts = {"text1", "text2", "text3"};
auto embeddings = provider->EmbedBatch(texts);

// 方法 2: 从配置文件加载
auto registry = EmbeddingConfig::LoadFromFile("config/embedding.json");
auto manager = std::make_shared<EmbeddingManager>(registry, vector_db);

// 使用 Ollama provider
manager->SetProvider("ollama");
manager->IndexText("doc1", "Machine learning is great");
```

### 检查服务可用性

```cpp
auto provider = std::make_shared<OllamaEmbeddingProvider>();

if (provider->IsAvailable()) {
    std::cout << "Ollama service is available" << std::endl;
    std::cout << "Model: " << provider->GetModel() << std::endl;
    std::cout << "Dimension: " << provider->GetDimension() << std::endl;
} else {
    std::cout << "Ollama service is not available" << std::endl;
}
```

### 自定义配置

```cpp
auto provider = std::make_shared<OllamaEmbeddingProvider>();

// 设置模型
provider->SetModel("mxbai-embed-large");

// 设置端点
provider->SetEndpoint("http://localhost:11434");

// 设置超时（秒）
provider->SetTimeout(60);
```

## 启动 Ollama 服务

### 前台运行
```bash
ollama serve
```

### 后台运行（systemd）
```bash
sudo systemctl start ollama
sudo systemctl enable ollama  # 开机自启
```

### 检查服务状态
```bash
curl http://localhost:11434/api/tags
```

## 性能对比

| Provider | 延迟 | 质量 | 成本 | 隐私 | 离线 |
|----------|------|------|------|------|------|
| Ollama (nomic-embed-text) | ~50ms | 优秀 | 免费 | ✅ | ✅ |
| Local (TF-IDF) | <1ms | 中等 | 免费 | ✅ | ✅ |
| OpenAI | 100-200ms | 优秀 | 付费 | ⚠️ | ❌ |
| Anthropic | 100-200ms | 优秀 | 付费 | ⚠️ | ❌ |

## 常见问题

### 1. 服务连接失败

**错误**: `Ollama API request failed`

**解决**:
```bash
# 检查服务是否运行
curl http://localhost:11434/api/tags

# 启动服务
ollama serve
```

### 2. 模型未找到

**错误**: `Model not found`

**解决**:
```bash
# 拉取模型
ollama pull nomic-embed-text

# 查看已安装模型
ollama list
```

### 3. 超时错误

**错误**: `Request timeout`

**解决**:
```cpp
// 增加超时时间
provider->SetTimeout(120);  // 120 秒
```

### 4. 端口冲突

**解决**:
```bash
# 使用自定义端口启动
OLLAMA_HOST=0.0.0.0:8080 ollama serve
```

然后更新配置：
```json
{
  "endpoint": "http://localhost:8080"
}
```

## 推荐配置

### 开发环境
```json
{
  "embedding": {
    "default_provider": "ollama",
    "providers": {
      "ollama": {
        "type": "ollama",
        "model": "nomic-embed-text",
        "endpoint": "http://localhost:11434"
      }
    }
  }
}
```

**优点**:
- 快速迭代
- 无需 API 密钥
- 高质量嵌入

### 生产环境
```json
{
  "embedding": {
    "default_provider": "ollama",
    "providers": {
      "ollama": {
        "type": "ollama",
        "model": "mxbai-embed-large",
        "endpoint": "http://localhost:11434",
        "timeout": 60
      },
      "local": {
        "type": "local",
        "dimension": 768
      }
    }
  }
}
```

**优点**:
- 高质量嵌入
- 本地 fallback
- 完全隐私保护

## 性能优化

### 1. GPU 加速

如果有 NVIDIA GPU：
```bash
# 安装 CUDA 版本
curl -fsSL https://ollama.com/install.sh | sh

# Ollama 会自动使用 GPU
ollama serve
```

### 2. 批量处理

```cpp
// 批量处理更高效
std::vector<std::string> texts = {...};  // 100 个文本
auto embeddings = provider->EmbedBatch(texts);
```

### 3. 缓存

```cpp
// 启用缓存
manager->SetCacheEnabled(true);
manager->SetMaxCacheSize(10000);
```

## 模型选择指南

| 场景 | 推荐模型 | 原因 |
|------|----------|------|
| 通用搜索 | nomic-embed-text | 平衡性能和质量 |
| 高精度搜索 | mxbai-embed-large | 最佳质量 |
| 资源受限 | all-minilm | 轻量快速 |
| 多语言 | nomic-embed-text | 支持多语言 |

## 与其他 Provider 对比

### Ollama vs Local (TF-IDF)
- **Ollama**: 更好的语义理解，稍高延迟
- **Local**: 极低延迟，适合简单场景

### Ollama vs OpenAI
- **Ollama**: 免费、隐私、离线
- **OpenAI**: 稍好的质量，需要网络和费用

### Ollama vs Anthropic
- **Ollama**: 免费、隐私、离线
- **Anthropic**: 专门优化的模型（如代码搜索）

## 最佳实践

1. **开发时使用 Ollama**
   - 快速迭代
   - 无成本
   - 高质量

2. **生产环境配置 fallback**
   ```json
   {
     "default_provider": "ollama",
     "providers": {
       "ollama": {...},
       "local": {...}  // fallback
     }
   }
   ```

3. **定期更新模型**
   ```bash
   ollama pull nomic-embed-text
   ```

4. **监控服务状态**
   ```cpp
   if (!provider->IsAvailable()) {
       // 切换到 fallback provider
       manager->SetProvider("local");
   }
   ```

## 参考资料

- [Ollama 官网](https://ollama.com/)
- [Ollama GitHub](https://github.com/ollama/ollama)
- [nomic-embed-text 模型](https://ollama.com/library/nomic-embed-text)
- [支持的模型列表](https://ollama.com/library)
