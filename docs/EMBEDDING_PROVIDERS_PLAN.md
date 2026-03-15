# QuantClaw Embedding 提供商 - Phase 3 实施计划

## 目标

实现多个 Embedding 提供商，为向量搜索系统提供文本向量化能力。

## 架构设计

### 1. 核心接口

```cpp
class EmbeddingProvider {
 public:
  virtual ~EmbeddingProvider() = default;

  // 单个文本向量化
  virtual std::vector<float> Embed(const std::string& text) = 0;

  // 批量文本向量化
  virtual std::vector<std::vector<float>> EmbedBatch(
      const std::vector<std::string>& texts) = 0;

  // 获取向量维度
  virtual int GetDimension() const = 0;

  // 获取提供商名称
  virtual std::string GetName() const = 0;
};
```

### 2. 提供商实现

#### 2.1 MockEmbeddingProvider (测试用)
- **用途**: 单元测试和开发
- **实现**: 简单的哈希函数生成固定维度向量
- **维度**: 可配置（默认 384）
- **特点**:
  - 确定性输出（相同输入产生相同向量）
  - 无需网络请求
  - 快速响应

#### 2.2 OpenAIEmbeddingProvider
- **模型**: text-embedding-3-small, text-embedding-3-large
- **维度**:
  - small: 1536
  - large: 3072
- **API**: OpenAI Embeddings API
- **特点**:
  - 高质量向量
  - 支持批量请求（最多 2048 个文本）
  - 速率限制处理

#### 2.3 AnthropicEmbeddingProvider
- **模型**: 使用 Voyage AI (Anthropic 推荐)
- **维度**: 1024
- **API**: Voyage AI API
- **特点**:
  - 针对 RAG 优化
  - 支持批量请求
  - 与 Claude 集成良好

### 3. 提供商管理器

```cpp
class EmbeddingProviderRegistry {
 public:
  // 注册提供商
  void RegisterProvider(const std::string& name,
                       std::shared_ptr<EmbeddingProvider> provider);

  // 获取提供商
  std::shared_ptr<EmbeddingProvider> GetProvider(const std::string& name);

  // 列出所有提供商
  std::vector<std::string> ListProviders() const;

  // 设置默认提供商
  void SetDefaultProvider(const std::string& name);

  // 获取默认提供商
  std::shared_ptr<EmbeddingProvider> GetDefaultProvider();
};
```

## 实施步骤

### Step 1: 核心接口 (30 分钟)
- [ ] 创建 `include/quantclaw/core/embedding_provider.hpp`
- [ ] 定义 `EmbeddingProvider` 基类
- [ ] 定义 `EmbeddingProviderRegistry` 类

### Step 2: MockEmbeddingProvider (30 分钟)
- [ ] 实现 `MockEmbeddingProvider`
- [ ] 使用简单哈希生成向量
- [ ] 编写单元测试

### Step 3: OpenAIEmbeddingProvider (1 小时)
- [ ] 实现 OpenAI API 调用
- [ ] 支持 text-embedding-3-small/large
- [ ] 处理批量请求
- [ ] 错误处理和重试
- [ ] 编写集成测试

### Step 4: AnthropicEmbeddingProvider (1 小时)
- [ ] 实现 Voyage AI API 调用
- [ ] 支持批量请求
- [ ] 错误处理和重试
- [ ] 编写集成测试

### Step 5: EmbeddingManager (30 分钟)
- [ ] 集成到 VectorDatabase
- [ ] 自动向量化文本
- [ ] 缓存机制

### Step 6: 配置和文档 (30 分钟)
- [ ] 配置文件支持
- [ ] API 文档
- [ ] 使用示例

## 配置示例

```json
{
  "embedding": {
    "default_provider": "openai",
    "providers": {
      "mock": {
        "type": "mock",
        "dimension": 384
      },
      "openai": {
        "type": "openai",
        "api_key": "${OPENAI_API_KEY}",
        "model": "text-embedding-3-small",
        "batch_size": 100
      },
      "anthropic": {
        "type": "voyage",
        "api_key": "${VOYAGE_API_KEY}",
        "model": "voyage-2",
        "batch_size": 128
      }
    }
  }
}
```

## 使用示例

```cpp
// 1. 创建提供商
auto mock_provider = std::make_shared<MockEmbeddingProvider>(384);
auto openai_provider = std::make_shared<OpenAIEmbeddingProvider>(
    api_key, "text-embedding-3-small");

// 2. 注册提供商
EmbeddingProviderRegistry registry;
registry.RegisterProvider("mock", mock_provider);
registry.RegisterProvider("openai", openai_provider);
registry.SetDefaultProvider("openai");

// 3. 使用提供商
auto provider = registry.GetDefaultProvider();
auto vector = provider->Embed("Hello, world!");

// 4. 批量向量化
std::vector<std::string> texts = {"text1", "text2", "text3"};
auto vectors = provider->EmbedBatch(texts);

// 5. 集成到 VectorDatabase
EmbeddingManager manager(registry, vector_db);
manager.IndexText("doc1", "This is a document");
auto results = manager.SearchText("query text", 10);
```

## 测试计划

### 单元测试
- [ ] MockEmbeddingProvider 测试
- [ ] EmbeddingProviderRegistry 测试
- [ ] 向量维度验证
- [ ] 批量处理测试

### 集成测试
- [ ] OpenAI API 调用测试（需要 API key）
- [ ] Voyage AI API 调用测试（需要 API key）
- [ ] 错误处理测试
- [ ] 重试机制测试

### 性能测试
- [ ] 批量向量化性能
- [ ] 缓存效果测试
- [ ] 并发请求测试

## 依赖项

- **libcurl**: HTTP 请求
- **nlohmann/json**: JSON 解析
- **OpenSSL**: HTTPS 支持

## 预期成果

1. **3 个 Embedding 提供商**
   - MockEmbeddingProvider (测试用)
   - OpenAIEmbeddingProvider
   - AnthropicEmbeddingProvider (Voyage AI)

2. **提供商管理**
   - 注册和发现机制
   - 默认提供商配置
   - 运行时切换

3. **完整测试**
   - 15+ 单元测试
   - 6+ 集成测试
   - 100% 代码覆盖

4. **文档**
   - API 文档
   - 配置指南
   - 使用示例

## 时间估算

- **总计**: 4-5 小时
- **核心接口**: 30 分钟
- **Mock 提供商**: 30 分钟
- **OpenAI 提供商**: 1 小时
- **Anthropic 提供商**: 1 小时
- **集成和测试**: 1-2 小时

---

**计划制定**: 2026-03-14
**预计完成**: 2026-03-14
**状态**: 📋 待实施
