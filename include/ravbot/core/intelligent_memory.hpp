// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

namespace ravbot {

// 记忆类型
enum class MemoryType {
  Knowledge,    // 知识
  Experience,   // 经验
  Code,         // 代码
  Issue,        // 问题
  Solution,     // 解决方案
  Concept       // 概念
};

// 开发层次
enum class DevelopmentLayer {
  Infrastructure,  // 基础设施
  Core,           // 核心
  Service,        // 服务
  Application,    // 应用
  General         // 通用
};

// 关系类型
enum class RelationType {
  DependsOn,    // 依赖
  RelatedTo,    // 相关
  Implements,   // 实现
  Solves,       // 解决
  Extends,      // 扩展
  Contradicts,  // 矛盾
  Supersedes,   // 取代
  References    // 引用
};

// 记忆元数据
struct MemoryMetadata {
  std::string id;
  std::string summary;
  DevelopmentLayer layer = DevelopmentLayer::General;
  MemoryType type = MemoryType::Knowledge;
  double importance = 0.5;
  std::string language = "en";
  std::string source;
  std::vector<std::string> topics;
  std::vector<std::string> tags;
  nlohmann::json extra;
};

// 记忆条目
struct MemoryEntry {
  std::string id;
  std::string content;
  std::string summary;
  std::string timestamp;
  DevelopmentLayer layer;
  MemoryType type;
  double importance;
  std::string language;
  std::string source;
  int access_count;
  std::string last_accessed;
  std::vector<std::string> topics;
  std::vector<std::string> tags;
};

// 话题信息
struct Topic {
  int id;
  std::string name;
  std::optional<int> parent_id;
  std::string description;
  int entry_count;
  std::string path;  // 层级路径，如 "development/cpp"
};

// 记忆关系
struct MemoryRelation {
  std::string source_id;
  std::string target_id;
  RelationType type;
  double weight;
  nlohmann::json metadata;
};

// 搜索查询
struct SearchQuery {
  std::string text;
  std::vector<std::string> topics;
  std::vector<std::string> tags;
  std::optional<DevelopmentLayer> layer;
  std::optional<MemoryType> type;
  std::optional<std::string> language;
  double min_importance = 0.0;
  int max_results = 10;
};

// 搜索模式
enum class SearchMode {
  Keyword,    // 关键词（BM25）
  Semantic,   // 语义（向量）
  Graph,      // 图谱
  Hybrid      // 混合
};

// 搜索结果
struct MemorySearchResult {
  MemoryEntry entry;
  double score;
  std::string match_reason;  // 匹配原因
};

// 智能记忆管理器
class IntelligentMemoryManager {
public:
  explicit IntelligentMemoryManager(
    const std::filesystem::path& workspace_path,
    std::shared_ptr<spdlog::logger> logger
  );
  ~IntelligentMemoryManager();

  // 初始化数据库
  bool Initialize();

  // 存储记忆（自动分类）
  std::string StoreMemory(
    const std::string& content,
    const MemoryMetadata& metadata
  );

  // 批量存储
  std::vector<std::string> StoreBatch(
    const std::vector<std::pair<std::string, MemoryMetadata>>& entries
  );

  // 检索记忆
  std::vector<MemorySearchResult> Search(
    const SearchQuery& query,
    SearchMode mode = SearchMode::Hybrid
  );

  // 获取记忆详情
  std::optional<MemoryEntry> GetMemory(const std::string& id);

  // 更新记忆
  bool UpdateMemory(const std::string& id, const MemoryMetadata& metadata);

  // 删除记忆
  bool DeleteMemory(const std::string& id);

  // 获取相关记忆（通过图谱）
  std::vector<MemorySearchResult> GetRelated(
    const std::string& memory_id,
    int max_depth = 2,
    int max_results = 10
  );

  // 添加关系
  bool AddRelation(
    const std::string& source_id,
    const std::string& target_id,
    RelationType type,
    double weight = 1.0,
    const nlohmann::json& metadata = {}
  );

  // 话题管理
  std::vector<Topic> GetTopics(const std::string& parent = "");
  std::vector<Topic> GetTopicHierarchy();
  std::optional<Topic> GetTopic(const std::string& name);
  std::vector<MemoryEntry> GetByTopic(const std::string& topic_name, int limit = 50);

  // 标签管理
  std::vector<std::string> GetTags(const std::string& category = "");
  std::vector<MemoryEntry> GetByTag(const std::string& tag_name, int limit = 50);

  // 统计信息
  nlohmann::json GetStats();

  // 自动分类（基于内容分析）
  std::vector<std::string> SuggestTopics(const std::string& content);
  std::vector<std::string> SuggestTags(const std::string& content);
  DevelopmentLayer SuggestLayer(const std::string& content);

  // 重要性评估
  double EvaluateImportance(const std::string& content, const MemoryMetadata& metadata);

  // 记忆压缩（总结旧记忆）
  bool CompressOldMemories(int days_threshold = 30);

private:
  std::filesystem::path workspace_path_;
  std::filesystem::path db_path_;
  std::shared_ptr<spdlog::logger> logger_;
  sqlite3* db_ = nullptr;

  // 数据库操作
  bool ExecuteSQL(const std::string& sql);
  std::optional<nlohmann::json> QueryOne(const std::string& sql);
  std::vector<nlohmann::json> QueryAll(const std::string& sql);

  // 辅助函数
  std::string GenerateUUID();
  std::string LayerToString(DevelopmentLayer layer);
  DevelopmentLayer StringToLayer(const std::string& str);
  std::string TypeToString(MemoryType type);
  MemoryType StringToType(const std::string& str);
  std::string RelationToString(RelationType type);
  RelationType StringToRelation(const std::string& str);

  // 搜索实现
  std::vector<MemorySearchResult> KeywordSearch(const SearchQuery& query);
  std::vector<MemorySearchResult> SemanticSearch(const SearchQuery& query);
  std::vector<MemorySearchResult> GraphSearch(const SearchQuery& query);
  std::vector<MemorySearchResult> HybridSearch(const SearchQuery& query);

  // 内容分析
  std::vector<std::string> ExtractKeywords(const std::string& content);
  std::string GenerateSummary(const std::string& content);
};

} // namespace ravbot
