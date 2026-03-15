# C++ 向量搜索记忆系统 - 技术方案

生成日期: 2026-03-14
项目: QuantClaw 长效记忆系统增强

---

## 📋 目标

为 QuantClaw 实现完整的向量搜索记忆系统,支持:
1. 语义搜索 - 基于向量相似度的智能检索
2. 嵌入模型 - 文本转向量的本地推理
3. 向量数据库 - 高性能向量存储和检索
4. 与现有 SQLite 记忆系统集成

## 🔍 技术调研结果

### 1. 向量搜索引擎

#### 方案 A: **HNSWlib** (推荐 ⭐⭐⭐⭐⭐)

**GitHub**: https://github.com/nmslib/hnswlib

**优势**:
- ✅ Header-only C++11 库,无依赖
- ✅ 极简集成,只需包含头文件
- ✅ 性能优异,内存占用低
- ✅ 支持增量索引构建和更新
- ✅ 支持元素删除和替换
- ✅ 线程安全,支持并发查询
- ✅ 支持自定义距离函数
- ✅ 成熟稳定,被广泛使用

**特性**:
```cpp
// 简单易用的 API
hnswlib::Index<float> index(space, dim);
index.init_index(max_elements, M=16, ef_construction=200);
index.add_items(data, ids);
auto results = index.knn_query(query, k=10);
```

**支持的距离**:
- L2 (欧几里得距离)
- Inner Product (内积)
- Cosine Similarity (余弦相似度)

**性能**:
- 200M SIFT 数据集测试通过
- 支持百万到十亿级别的向量

**集成难度**: ⭐ (极简单)

---

#### 方案 B: **USearch** (推荐 ⭐⭐⭐⭐⭐)

**GitHub**: https://github.com/unum-cloud/usearch

**优势**:
- ✅ 单文件 C++11 header-only 库
- ✅ 比 FAISS 快 10x,比 HNSWlib 更快
- ✅ 支持多种语言绑定
- ✅ 支持 f16/i8/b1 量化
- ✅ 支持自定义距离函数
- ✅ 支持过滤谓词
- ✅ 支持磁盘映射(mmap)
- ✅ 被 ClickHouse、DuckDB、ScyllaDB 等使用

**特性**:
```cpp
// 极简 API
using namespace unum::usearch;
index_dense_t index = index_dense_t::make(metric_kind_t::cos_k);
index.reserve(capacity);
index.add(key, vector);
auto results = index.search(vector, k);
```

**支持的距离**:
- L2, IP, Cosine
- Haversine (地理坐标)
- Tanimoto, Sorensen (化学分子)
- Hamming, Jaccard (二进制)

**性能对比 (vs FAISS)**:
- 索引速度: 9.6x - 10.7x 更快
- 代码量: 3K SLOC vs 84K SLOC
- 内存占用: 更低

**集成难度**: ⭐ (极简单)

---

#### 方案 C: **FAISS**

**GitHub**: https://github.com/facebookresearch/faiss

**优势**:
- ✅ Facebook 开发,工业级标准
- ✅ 功能最全面
- ✅ GPU 加速支持
- ✅ 多种索引类型

**劣势**:
- ❌ 依赖 BLAS/LAPACK
- ❌ 代码量大,集成复杂
- ❌ 编译时间长

**集成难度**: ⭐⭐⭐⭐ (复杂)

---

### 2. SQLite 向量扩展

#### 方案 D: **sqlite-vec** (推荐 ⭐⭐⭐⭐⭐)

**GitHub**: https://github.com/asg017/sqlite-vec

**优势**:
- ✅ 纯 C 实现,无依赖
- ✅ 直接集成到 SQLite
- ✅ 支持 float/int8/binary 向量
- ✅ 支持元数据列
- ✅ 跨平台(Linux/macOS/Windows/WASM)
- ✅ Mozilla 赞助项目

**特性**:
```sql
-- 创建向量表
CREATE VIRTUAL TABLE vec_items USING vec0(
  embedding float[384]
);

-- 插入向量
INSERT INTO vec_items(rowid, embedding)
VALUES (1, '[0.1, 0.2, ...]');

-- KNN 查询
SELECT rowid, distance
FROM vec_items
WHERE embedding MATCH '[0.5, 0.3, ...]'
ORDER BY distance
LIMIT 10;
```

**集成方式**:
- 作为 SQLite 扩展加载
- 可以与现有 MemoryManager 无缝集成

**集成难度**: ⭐⭐ (简单)

---

#### 方案 E: **sqlite-vss** (已废弃)

**GitHub**: https://github.com/asg017/sqlite-vss

**状态**: ⚠️ 不再维护,作者推荐使用 sqlite-vec

---

### 3. 嵌入模型推理

#### 方案 F: **ONNX Runtime** (推荐 ⭐⭐⭐⭐⭐)

**官网**: https://onnxruntime.ai/

**优势**:
- ✅ 微软开发,工业级标准
- ✅ 支持多种框架模型(PyTorch/TensorFlow/HuggingFace)
- ✅ 跨平台,硬件加速
- ✅ C++ API 完善
- ✅ 支持量化和优化

**使用流程**:
1. 将 Sentence-Transformers 模型导出为 ONNX 格式
2. 使用 ONNX Runtime C++ API 加载模型
3. 执行推理生成向量

**示例代码**:
```cpp
#include <onnxruntime_cxx_api.h>

// 创建环境和会话
Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "embedding");
Ort::SessionOptions session_options;
Ort::Session session(env, "model.onnx", session_options);

// 准备输入
std::vector<int64_t> input_ids = tokenize(text);
Ort::Value input_tensor = Ort::Value::CreateTensor<int64_t>(...);

// 执行推理
auto output_tensors = session.Run(..., input_names, &input_tensor, 1, output_names, 1);

// 获取嵌入向量
float* embeddings = output_tensors[0].GetTensorMutableData<float>();
```

**支持的模型**:
- all-MiniLM-L6-v2 (384 维)
- all-mpnet-base-v2 (768 维)
- text-embedding-3-small (1536 维)
- 任何 ONNX 格式的嵌入模型

**集成难度**: ⭐⭐⭐ (中等)

---

#### 方案 G: **llama.cpp / bert.cpp**

**GitHub**:
- https://github.com/ggerganov/llama.cpp
- https://github.com/skeskinen/bert.cpp

**优势**:
- ✅ 纯 C++ 实现
- ✅ 支持 GGUF 格式模型
- ✅ CPU 优化(SIMD)

**劣势**:
- ❌ 主要针对 LLM,嵌入模型支持有限
- ❌ 需要转换模型格式

**集成难度**: ⭐⭐⭐⭐ (较复杂)

---

## 🎯 推荐方案

### 方案组合 1: **USearch + ONNX Runtime** (最佳性能)

**架构**:
```
文本输入
  ↓
ONNX Runtime (嵌入模型推理)
  ↓
向量 (float[384])
  ↓
USearch (向量索引和搜索)
  ↓
搜索结果
```

**优势**:
- ✅ 最佳性能(USearch 比 FAISS 快 10x)
- ✅ 灵活的嵌入模型选择
- ✅ 支持硬件加速
- ✅ 工业级稳定性

**实现复杂度**: ⭐⭐⭐ (中等)

---

### 方案组合 2: **sqlite-vec + ONNX Runtime** (最佳集成)

**架构**:
```
文本输入
  ↓
ONNX Runtime (嵌入模型推理)
  ↓
向量 (float[384])
  ↓
SQLite + sqlite-vec (向量存储和搜索)
  ↓
搜索结果
```

**优势**:
- ✅ 与现有 SQLite 记忆系统无缝集成
- ✅ SQL 查询接口,易于使用
- ✅ 支持元数据过滤
- ✅ 事务支持

**实现复杂度**: ⭐⭐ (简单)

---

### 方案组合 3: **HNSWlib + ONNX Runtime** (最简单)

**架构**:
```
文本输入
  ↓
ONNX Runtime (嵌入模型推理)
  ↓
向量 (float[384])
  ↓
HNSWlib (向量索引和搜索)
  ↓
搜索结果
```

**优势**:
- ✅ 最简单集成(header-only)
- ✅ 成熟稳定
- ✅ 性能优异

**实现复杂度**: ⭐⭐ (简单)

---

## 📐 系统架构设计

### 推荐架构: **混合方案**

结合 USearch 和 sqlite-vec 的优势:

```
┌─────────────────────────────────────────────────────────┐
│                    QuantClaw 记忆系统                      │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────┐      ┌──────────────┐               │
│  │ 文本输入      │ ───> │ EmbeddingManager│             │
│  └──────────────┘      └──────────────┘               │
│                              │                          │
│                              ↓                          │
│                    ┌──────────────────┐                │
│                    │ ONNX Runtime     │                │
│                    │ (嵌入模型推理)    │                │
│                    └──────────────────┘                │
│                              │                          │
│                              ↓                          │
│                    向量 (float[384])                    │
│                              │                          │
│              ┌───────────────┴───────────────┐         │
│              ↓                               ↓         │
│    ┌──────────────────┐          ┌──────────────────┐ │
│    │ USearch Index    │          │ SQLite + vec0    │ │
│    │ (快速搜索)        │          │ (持久化存储)      │ │
│    └──────────────────┘          └──────────────────┘ │
│              │                               │         │
│              └───────────────┬───────────────┘         │
│                              ↓                          │
│                    ┌──────────────────┐                │
│                    │ VectorDatabase   │                │
│                    │ (统一接口)        │                │
│                    └──────────────────┘                │
│                              │                          │
│                              ↓                          │
│                        搜索结果                          │
└─────────────────────────────────────────────────────────┘
```

**工作流程**:

1. **索引阶段**:
   - 文本 → ONNX Runtime → 向量
   - 向量 → USearch (内存索引,快速搜索)
   - 向量 → SQLite (持久化存储)

2. **搜索阶段**:
   - 查询文本 → ONNX Runtime → 查询向量
   - 查询向量 → USearch → Top-K 候选
   - 候选 → SQLite → 获取完整元数据
   - 返回结果

---

## 🛠️ 实现计划

### 阶段 1: 基础向量搜索 (1-2 周)

**任务**:
1. 集成 USearch 或 HNSWlib
2. 实现 VectorIndex 类
3. 基本的添加、搜索、删除功能
4. 单元测试

**文件**:
- `include/quantclaw/core/vector_index.hpp`
- `src/core/vector_index.cpp`
- `tests/test_vector_index.cpp`

**API 设计**:
```cpp
class VectorIndex {
public:
  VectorIndex(int dim, const std::string& metric = "cosine");

  // 添加向量
  void add(const std::string& id, const std::vector<float>& vector);

  // 搜索
  std::vector<SearchResult> search(const std::vector<float>& query, int k);

  // 删除
  void remove(const std::string& id);

  // 持久化
  void save(const std::string& path);
  void load(const std::string& path);
};
```

---

### 阶段 2: 嵌入模型集成 (2-3 周)

**任务**:
1. 集成 ONNX Runtime
2. 实现 EmbeddingManager 类
3. 支持模型加载和推理
4. 文本预处理(分词)
5. 单元测试

**文件**:
- `include/quantclaw/core/embedding_manager.hpp` (已存在)
- `src/core/embedding_manager.cpp`
- `tests/test_embedding_manager.cpp`

**API 设计**:
```cpp
class EmbeddingManager {
public:
  EmbeddingManager(const std::string& model_path);

  // 生成嵌入向量
  std::vector<float> embed(const std::string& text);

  // 批量生成
  std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts);

  // 获取维度
  int dimension() const;
};
```

---

### 阶段 3: SQLite 集成 (1-2 周)

**任务**:
1. 集成 sqlite-vec 扩展
2. 扩展 VectorDatabase 类
3. 实现 SQL 查询接口
4. 元数据支持
5. 单元测试

**文件**:
- `include/quantclaw/core/vector_database.hpp` (已存在)
- `src/core/vector_database.cpp` (已存在)
- `tests/test_vector_database.cpp`

**API 设计**:
```cpp
class VectorDatabase {
public:
  VectorDatabase(const std::string& db_path);

  // 添加向量(带元数据)
  void add(const std::string& id,
           const std::vector<float>& vector,
           const nlohmann::json& metadata);

  // 语义搜索
  std::vector<SearchResult> semantic_search(
    const std::string& query_text,
    int k,
    const std::string& filter = ""
  );

  // 向量搜索
  std::vector<SearchResult> vector_search(
    const std::vector<float>& query_vector,
    int k,
    const std::string& filter = ""
  );
};
```

---

### 阶段 4: 记忆系统集成 (1 周)

**任务**:
1. 扩展 MemoryManager
2. 实现语义搜索接口
3. 与现有记忆系统集成
4. 端到端测试

**文件**:
- `include/quantclaw/core/memory_manager.hpp` (修改)
- `src/core/memory_manager.cpp` (修改)
- `tests/test_memory_semantic_search.cpp`

**API 扩展**:
```cpp
class MemoryManager {
public:
  // 现有方法...

  // 新增: 语义搜索
  std::vector<Message> SemanticSearch(
    const std::string& query,
    int limit = 10,
    const std::string& session_id = ""
  );

  // 新增: 添加消息时自动生成嵌入
  void SaveMessageWithEmbedding(
    const std::string& session_id,
    const std::string& role,
    const std::string& content
  );
};
```

---

## 📦 依赖管理

### CMakeLists.txt 修改

```cmake
# ONNX Runtime
find_package(onnxruntime REQUIRED)

# USearch (header-only)
FetchContent_Declare(
  usearch
  GIT_REPOSITORY https://github.com/unum-cloud/usearch.git
  GIT_TAG        v2.24.0
)
FetchContent_MakeAvailable(usearch)

# sqlite-vec (作为扩展加载)
# 编译时包含,运行时加载

target_link_libraries(quantclaw_core
  PRIVATE
    onnxruntime::onnxruntime
)

target_include_directories(quantclaw_core
  PRIVATE
    ${usearch_SOURCE_DIR}/include
)
```

---

## 🧪 测试策略

### 单元测试

1. **VectorIndex 测试**:
   - 添加/搜索/删除
   - 持久化/加载
   - 并发操作
   - 性能测试

2. **EmbeddingManager 测试**:
   - 模型加载
   - 文本嵌入
   - 批量处理
   - 性能测试

3. **VectorDatabase 测试**:
   - SQL 查询
   - 元数据过滤
   - 事务支持
   - 性能测试

### 集成测试

1. **端到端测试**:
   - 文本 → 嵌入 → 索引 → 搜索
   - 多会话记忆
   - 大规模数据测试

### 性能基准

1. **索引性能**:
   - 1K/10K/100K/1M 向量
   - 索引构建时间
   - 内存占用

2. **搜索性能**:
   - QPS (每秒查询数)
   - 延迟 (P50/P95/P99)
   - 召回率

---

## 📊 预期性能

### 向量搜索性能 (USearch)

| 数据规模 | 索引时间 | 搜索延迟 (P95) | 内存占用 |
|---------|---------|---------------|---------|
| 10K     | < 1s    | < 1ms         | ~10 MB  |
| 100K    | < 10s   | < 2ms         | ~100 MB |
| 1M      | < 2min  | < 5ms         | ~1 GB   |
| 10M     | < 20min | < 10ms        | ~10 GB  |

### 嵌入推理性能 (ONNX Runtime)

| 模型 | 维度 | CPU 延迟 | GPU 延迟 |
|------|------|---------|---------|
| MiniLM-L6 | 384 | ~10ms | ~2ms |
| MPNet-base | 768 | ~20ms | ~5ms |
| text-embedding-3 | 1536 | ~50ms | ~10ms |

---

## 💰 成本估算

### 开发时间

| 阶段 | 时间 | 人力 |
|------|------|------|
| 阶段 1: 基础向量搜索 | 1-2 周 | 1 人 |
| 阶段 2: 嵌入模型集成 | 2-3 周 | 1 人 |
| 阶段 3: SQLite 集成 | 1-2 周 | 1 人 |
| 阶段 4: 记忆系统集成 | 1 周 | 1 人 |
| **总计** | **5-8 周** | **1 人** |

### 运行成本

- **存储**: 每 1M 向量 ~1 GB (float32)
- **内存**: 每 1M 向量 ~1 GB (索引)
- **计算**: CPU 推理,无额外成本

---

## 🎓 学习资源

### 向量搜索

1. **HNSW 算法论文**: https://arxiv.org/abs/1603.09320
2. **USearch 文档**: https://unum-cloud.github.io/USearch/
3. **HNSWlib 文档**: https://github.com/nmslib/hnswlib

### 嵌入模型

1. **Sentence-Transformers**: https://www.sbert.net/
2. **ONNX Runtime 文档**: https://onnxruntime.ai/docs/
3. **模型导出教程**: https://huggingface.co/docs/optimum/exporters/onnx/usage_guides/export_a_model

### SQLite 向量扩展

1. **sqlite-vec 文档**: https://alexgarcia.xyz/sqlite-vec/
2. **SQLite 扩展开发**: https://www.sqlite.org/loadext.html

---

## 🚀 快速开始

### 最小可行原型 (MVP)

使用 HNSWlib + 简单的文本嵌入:

```cpp
#include "hnswlib/hnswlib.h"
#include <vector>
#include <string>

class SimpleVectorMemory {
public:
  SimpleVectorMemory(int dim) : dim_(dim) {
    index_ = new hnswlib::Index<float>(hnswlib::L2Space(dim), 1000);
  }

  void add(const std::string& text, const std::vector<float>& embedding) {
    int id = texts_.size();
    texts_.push_back(text);
    index_->addPoint(embedding.data(), id);
  }

  std::vector<std::string> search(const std::vector<float>& query, int k) {
    auto result = index_->searchKnn(query.data(), k);
    std::vector<std::string> results;
    while (!result.empty()) {
      int id = result.top().second;
      results.push_back(texts_[id]);
      result.pop();
    }
    return results;
  }

private:
  int dim_;
  hnswlib::Index<float>* index_;
  std::vector<std::string> texts_;
};
```

---

## 📝 总结

### 推荐方案

**最佳方案**: **USearch + ONNX Runtime + sqlite-vec**

**理由**:
1. ✅ USearch 提供最佳性能
2. ✅ ONNX Runtime 提供灵活的模型支持
3. ✅ sqlite-vec 提供与现有系统的无缝集成
4. ✅ 所有组件都是开源且成熟稳定
5. ✅ 实现复杂度适中

### 实施路线

1. **第一步**: 集成 HNSWlib 或 USearch (1-2 周)
2. **第二步**: 集成 ONNX Runtime (2-3 周)
3. **第三步**: 集成 sqlite-vec (1-2 周)
4. **第四步**: 与 MemoryManager 集成 (1 周)

**总时间**: 5-8 周

### 预期效果

- ✅ 支持语义搜索
- ✅ 支持百万级向量
- ✅ 搜索延迟 < 10ms
- ✅ 与现有系统无缝集成
- ✅ 完整的测试覆盖

---

**报告日期**: 2026-03-14
**报告人**: team-lead
**状态**: 待审批
