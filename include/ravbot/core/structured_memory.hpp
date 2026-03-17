// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace ravbot {

// 记忆分类维度
enum class MemoryDimension {
  TOPIC,        // 话题：技术栈、项目、领域
  KNOWLEDGE,    // 知识结构：概念、原理、实践
  LAYER,        // 开发层：架构、实现、调试、部署
  GENERAL       // 通识：常识、经验、模式
};

// 记忆条目元数据
struct MemoryMetadata {
  std::string id;                                    // 唯一标识
  std::string title;                                 // 标题
  std::vector<std::string> tags;                     // 标签
  std::unordered_map<MemoryDimension, std::string> dimensions;  // 维度分类
  std::string timestamp;                             // 时间戳
  int importance = 0;                                // 重要性 (0-10)
  std::vector<std::string> related_ids;              // 关联记忆ID
};

// 结构化记忆条目
struct StructuredMemoryEntry {
  MemoryMetadata metadata;
  std::string content;
  std::vector<std::string> tokens;  // 用于BM25搜索
};

// 搜索结果
struct StructuredSearchResult {
  std::string id;
  std::string title;
  std::string content;
  double score;
  MemoryMetadata metadata;
};

// 结构化记忆管理器
class StructuredMemory {
 public:
  explicit StructuredMemory(const std::filesystem::path& storage_path,
                           std::shared_ptr<spdlog::logger> logger);

  // 添加记忆条目
  std::string AddMemory(const std::string& title,
                       const std::string& content,
                       const std::vector<std::string>& tags,
                       const std::unordered_map<MemoryDimension, std::string>& dimensions,
                       int importance = 5);

  // 更新记忆条目
  bool UpdateMemory(const std::string& id,
                   const std::string& content,
                   const MemoryMetadata* metadata = nullptr);

  // 删除记忆条目
  bool DeleteMemory(const std::string& id);

  // 按维度搜索
  std::vector<StructuredSearchResult> SearchByDimension(
      MemoryDimension dimension,
      const std::string& value,
      int max_results = 10) const;

  // 按标签搜索
  std::vector<StructuredSearchResult> SearchByTags(
      const std::vector<std::string>& tags,
      int max_results = 10) const;

  // 全文搜索（BM25）
  std::vector<StructuredSearchResult> SearchFullText(
      const std::string& query,
      int max_results = 10) const;

  // 混合搜索（维度 + 全文）
  std::vector<StructuredSearchResult> HybridSearch(
      const std::string& query,
      const std::unordered_map<MemoryDimension, std::string>& dimension_filters,
      const std::vector<std::string>& tag_filters,
      int max_results = 10) const;

  // 获取关联记忆
  std::vector<StructuredSearchResult> GetRelatedMemories(
      const std::string& id,
      int max_results = 5) const;

  // 按重要性排序
  std::vector<StructuredSearchResult> GetTopMemories(
      int max_results = 10) const;

  // 导出为JSON
  nlohmann::json ExportToJson() const;

  // 从JSON导入
  void ImportFromJson(const nlohmann::json& data);

  // 保存到磁盘
  void Save();

  // 从磁盘加载
  void Load();

  // 统计信息
  nlohmann::json GetStats() const;

 private:
  // 生成唯一ID
  std::string GenerateId() const;

  // 分词
  static std::vector<std::string> Tokenize(const std::string& text);

  // BM25评分
  double ScoreBM25(const StructuredMemoryEntry& entry,
                  const std::vector<std::string>& query_tokens) const;

  // 计算文档频率
  int DocumentFrequency(const std::string& term) const;

  std::filesystem::path storage_path_;
  std::shared_ptr<spdlog::logger> logger_;
  std::unordered_map<std::string, StructuredMemoryEntry> entries_;
  
  // 索引
  std::unordered_map<MemoryDimension, 
                    std::unordered_map<std::string, std::vector<std::string>>> dimension_index_;
  std::unordered_map<std::string, std::vector<std::string>> tag_index_;
  
  // BM25参数
  double avg_doc_length_ = 0;
  static constexpr double kBM25_k1 = 1.2;
  static constexpr double kBM25_b = 0.75;
};

// 维度转字符串
std::string DimensionToString(MemoryDimension dim);

// 字符串转维度
MemoryDimension StringToDimension(const std::string& str);

}  // namespace ravbot
