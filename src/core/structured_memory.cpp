// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/core/structured_memory.hpp"
#include <algorithm>
#include <sstream>
#include <regex>
#include <fstream>
#include <random>
#include <iomanip>
#include <chrono>
#include <unordered_set>

namespace ravbot {

// MemoryDimension 转字符串
std::string DimensionToString(MemoryDimension dim) {
  switch (dim) {
    case MemoryDimension::TOPIC: return "topic";
    case MemoryDimension::KNOWLEDGE: return "knowledge";
    case MemoryDimension::LAYER: return "layer";
    case MemoryDimension::GENERAL: return "general";
    default: return "general";
  }
}

// 字符串转 MemoryDimension
MemoryDimension StringToDimension(const std::string& str) {
  if (str == "topic") return MemoryDimension::TOPIC;
  if (str == "knowledge") return MemoryDimension::KNOWLEDGE;
  if (str == "layer") return MemoryDimension::LAYER;
  if (str == "general") return MemoryDimension::GENERAL;
  return MemoryDimension::GENERAL;
}

// StructuredMemory 实现
StructuredMemory::StructuredMemory(const std::filesystem::path& storage_path,
                                   std::shared_ptr<spdlog::logger> logger)
    : storage_path_(storage_path), logger_(std::move(logger)) {
  if (!std::filesystem::exists(storage_path_)) {
    std::filesystem::create_directories(storage_path_);
  }
  Load();
}

std::string StructuredMemory::AddMemory(
    const std::string& title,
    const std::string& content,
    const std::vector<std::string>& tags,
    const std::unordered_map<MemoryDimension, std::string>& dimensions,
    int importance) {

  StructuredMemoryEntry entry;
  entry.metadata.id = GenerateId();
  entry.metadata.title = title;
  entry.metadata.tags = tags;
  entry.metadata.dimensions = dimensions;
  entry.metadata.timestamp = std::to_string(
      std::chrono::system_clock::now().time_since_epoch().count());
  entry.metadata.importance = importance;
  entry.content = content;
  entry.tokens = Tokenize(content + " " + title);

  entries_[entry.metadata.id] = entry;

  // 更新索引
  for (const auto& [dim, value] : dimensions) {
    dimension_index_[dim][value].push_back(entry.metadata.id);
  }

  for (const auto& tag : tags) {
    tag_index_[tag].push_back(entry.metadata.id);
  }

  // 更新平均文档长度
  avg_doc_length_ = 0;
  for (const auto& [id, e] : entries_) {
    avg_doc_length_ += e.tokens.size();
  }
  if (!entries_.empty()) {
    avg_doc_length_ /= entries_.size();
  }

  logger_->info("Added memory: id={}, title={}", entry.metadata.id, title);
  Save();

  return entry.metadata.id;
}

bool StructuredMemory::UpdateMemory(const std::string& id,
                                   const std::string& content,
                                   const MemoryMetadata* metadata) {
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return false;
  }

  // 移除旧索引
  for (const auto& [dim, value] : it->second.metadata.dimensions) {
    auto& vec = dimension_index_[dim][value];
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  }

  for (const auto& tag : it->second.metadata.tags) {
    auto& vec = tag_index_[tag];
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  }

  // 更新内容
  it->second.content = content;
  it->second.tokens = Tokenize(content + " " + it->second.metadata.title);

  if (metadata) {
    it->second.metadata = *metadata;
  }

  // 重建索引
  for (const auto& [dim, value] : it->second.metadata.dimensions) {
    dimension_index_[dim][value].push_back(id);
  }

  for (const auto& tag : it->second.metadata.tags) {
    tag_index_[tag].push_back(id);
  }

  logger_->info("Updated memory: id={}", id);
  Save();

  return true;
}

bool StructuredMemory::DeleteMemory(const std::string& id) {
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return false;
  }

  // 移除索引
  for (const auto& [dim, value] : it->second.metadata.dimensions) {
    auto& vec = dimension_index_[dim][value];
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  }

  for (const auto& tag : it->second.metadata.tags) {
    auto& vec = tag_index_[tag];
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  }

  entries_.erase(it);

  logger_->info("Deleted memory: id={}", id);
  Save();

  return true;
}

std::vector<StructuredSearchResult> StructuredMemory::SearchByDimension(
    MemoryDimension dimension,
    const std::string& value,
    int max_results) const {

  std::vector<StructuredSearchResult> results;

  auto dim_it = dimension_index_.find(dimension);
  if (dim_it == dimension_index_.end()) {
    return results;
  }

  auto val_it = dim_it->second.find(value);
  if (val_it == dim_it->second.end()) {
    return results;
  }

  int count = 0;
  for (const auto& id : val_it->second) {
    if (max_results > 0 && count >= max_results) break;

    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = id;
      result.title = entry_it->second.metadata.title;
      result.content = entry_it->second.content;
      result.score = 1.0;
      result.metadata = entry_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

std::vector<StructuredSearchResult> StructuredMemory::SearchByTags(
    const std::vector<std::string>& tags,
    int max_results) const {

  std::unordered_map<std::string, int> id_scores;

  for (const auto& tag : tags) {
    auto it = tag_index_.find(tag);
    if (it != tag_index_.end()) {
      for (const auto& id : it->second) {
        id_scores[id]++;
      }
    }
  }

  std::vector<std::pair<std::string, int>> sorted_ids(id_scores.begin(), id_scores.end());
  std::sort(sorted_ids.begin(), sorted_ids.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<StructuredSearchResult> results;
  int count = 0;
  for (const auto& [id, score] : sorted_ids) {
    if (max_results > 0 && count >= max_results) break;

    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = id;
      result.title = entry_it->second.metadata.title;
      result.content = entry_it->second.content;
      result.score = static_cast<double>(score) / tags.size();
      result.metadata = entry_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

std::vector<StructuredSearchResult> StructuredMemory::SearchFullText(
    const std::string& query,
    int max_results) const {

  auto query_tokens = Tokenize(query);

  std::vector<std::pair<std::string, double>> scored_entries;

  for (const auto& [id, entry] : entries_) {
    double score = ScoreBM25(entry, query_tokens);
    if (score > 0) {
      scored_entries.emplace_back(id, score);
    }
  }

  std::sort(scored_entries.begin(), scored_entries.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<StructuredSearchResult> results;
  int count = 0;
  for (const auto& [id, score] : scored_entries) {
    if (max_results > 0 && count >= max_results) break;

    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = id;
      result.title = entry_it->second.metadata.title;
      result.content = entry_it->second.content;
      result.score = score;
      result.metadata = entry_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

std::vector<StructuredSearchResult> StructuredMemory::HybridSearch(
    const std::string& query,
    const std::unordered_map<MemoryDimension, std::string>& dimension_filters,
    const std::vector<std::string>& tag_filters,
    int max_results) const {

  // 先通过维度和标签过滤
  std::unordered_set<std::string> candidate_ids;

  if (!dimension_filters.empty()) {
    for (const auto& [dim, value] : dimension_filters) {
      auto dim_it = dimension_index_.find(dim);
      if (dim_it != dimension_index_.end()) {
        auto val_it = dim_it->second.find(value);
        if (val_it != dim_it->second.end()) {
          for (const auto& id : val_it->second) {
            candidate_ids.insert(id);
          }
        }
      }
    }
  } else {
    for (const auto& [id, _] : entries_) {
      candidate_ids.insert(id);
    }
  }

  if (!tag_filters.empty()) {
    std::unordered_set<std::string> tag_filtered;
    for (const auto& tag : tag_filters) {
      auto it = tag_index_.find(tag);
      if (it != tag_index_.end()) {
        for (const auto& id : it->second) {
          if (candidate_ids.count(id)) {
            tag_filtered.insert(id);
          }
        }
      }
    }
    candidate_ids = tag_filtered;
  }

  // 对候选集进行全文搜索
  auto query_tokens = Tokenize(query);
  std::vector<std::pair<std::string, double>> scored_entries;

  for (const auto& id : candidate_ids) {
    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      double score = ScoreBM25(entry_it->second, query_tokens);
      scored_entries.emplace_back(id, score);
    }
  }

  std::sort(scored_entries.begin(), scored_entries.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<StructuredSearchResult> results;
  int count = 0;
  for (const auto& [id, score] : scored_entries) {
    if (max_results > 0 && count >= max_results) break;

    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = id;
      result.title = entry_it->second.metadata.title;
      result.content = entry_it->second.content;
      result.score = score;
      result.metadata = entry_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

std::vector<StructuredSearchResult> StructuredMemory::GetRelatedMemories(
    const std::string& id,
    int max_results) const {

  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return {};
  }

  std::vector<StructuredSearchResult> results;
  int count = 0;

  for (const auto& related_id : it->second.metadata.related_ids) {
    if (max_results > 0 && count >= max_results) break;

    auto related_it = entries_.find(related_id);
    if (related_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = related_id;
      result.title = related_it->second.metadata.title;
      result.content = related_it->second.content;
      result.score = 1.0;
      result.metadata = related_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

std::vector<StructuredSearchResult> StructuredMemory::GetTopMemories(
    int max_results) const {

  std::vector<std::pair<std::string, int>> sorted_entries;

  for (const auto& [id, entry] : entries_) {
    sorted_entries.emplace_back(id, entry.metadata.importance);
  }

  std::sort(sorted_entries.begin(), sorted_entries.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::vector<StructuredSearchResult> results;
  int count = 0;
  for (const auto& [id, importance] : sorted_entries) {
    if (max_results > 0 && count >= max_results) break;

    auto entry_it = entries_.find(id);
    if (entry_it != entries_.end()) {
      StructuredSearchResult result;
      result.id = id;
      result.title = entry_it->second.metadata.title;
      result.content = entry_it->second.content;
      result.score = importance / 10.0;
      result.metadata = entry_it->second.metadata;
      results.push_back(result);
      count++;
    }
  }

  return results;
}

nlohmann::json StructuredMemory::ExportToJson() const {
  nlohmann::json j;
  j["entries"] = nlohmann::json::array();

  for (const auto& [id, entry] : entries_) {
    nlohmann::json entry_json;
    entry_json["id"] = entry.metadata.id;
    entry_json["title"] = entry.metadata.title;
    entry_json["content"] = entry.content;
    entry_json["tags"] = entry.metadata.tags;
    entry_json["timestamp"] = entry.metadata.timestamp;
    entry_json["importance"] = entry.metadata.importance;
    entry_json["related_ids"] = entry.metadata.related_ids;

    nlohmann::json dims = nlohmann::json::object();
    for (const auto& [dim, value] : entry.metadata.dimensions) {
      dims[DimensionToString(dim)] = value;
    }
    entry_json["dimensions"] = dims;

    j["entries"].push_back(entry_json);
  }

  return j;
}

void StructuredMemory::ImportFromJson(const nlohmann::json& data) {
  entries_.clear();
  dimension_index_.clear();
  tag_index_.clear();

  if (!data.contains("entries") || !data["entries"].is_array()) {
    return;
  }

  for (const auto& entry_json : data["entries"]) {
    StructuredMemoryEntry entry;
    entry.metadata.id = entry_json.value("id", "");
    entry.metadata.title = entry_json.value("title", "");
    entry.content = entry_json.value("content", "");
    entry.metadata.tags = entry_json.value("tags", std::vector<std::string>{});
    entry.metadata.timestamp = entry_json.value("timestamp", "");
    entry.metadata.importance = entry_json.value("importance", 5);
    entry.metadata.related_ids = entry_json.value("related_ids", std::vector<std::string>{});

    if (entry_json.contains("dimensions") && entry_json["dimensions"].is_object()) {
      for (auto& [key, value] : entry_json["dimensions"].items()) {
        auto dim = StringToDimension(key);
        entry.metadata.dimensions[dim] = value.get<std::string>();
      }
    }

    entry.tokens = Tokenize(entry.content + " " + entry.metadata.title);

    entries_[entry.metadata.id] = entry;

    // 重建索引
    for (const auto& [dim, value] : entry.metadata.dimensions) {
      dimension_index_[dim][value].push_back(entry.metadata.id);
    }

    for (const auto& tag : entry.metadata.tags) {
      tag_index_[tag].push_back(entry.metadata.id);
    }
  }

  // 更新平均文档长度
  avg_doc_length_ = 0;
  for (const auto& [id, e] : entries_) {
    avg_doc_length_ += e.tokens.size();
  }
  if (!entries_.empty()) {
    avg_doc_length_ /= entries_.size();
  }

  logger_->info("Imported {} memories", entries_.size());
}

void StructuredMemory::Save() {
  auto json_data = ExportToJson();
  auto file_path = storage_path_ / "structured_memory.json";

  std::ofstream file(file_path);
  if (file.is_open()) {
    file << json_data.dump(2);
    logger_->debug("Saved memories to {}", file_path.string());
  } else {
    logger_->error("Failed to save memories to {}", file_path.string());
  }
}

void StructuredMemory::Load() {
  auto file_path = storage_path_ / "structured_memory.json";

  if (!std::filesystem::exists(file_path)) {
    logger_->info("No existing memory file found at {}", file_path.string());
    return;
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    logger_->error("Failed to load memories from {}", file_path.string());
    return;
  }

  try {
    nlohmann::json j;
    file >> j;
    ImportFromJson(j);
    logger_->info("Loaded memories from {}", file_path.string());
  } catch (const std::exception& e) {
    logger_->error("Failed to parse memory file: {}", e.what());
  }
}

nlohmann::json StructuredMemory::GetStats() const {
  nlohmann::json stats;
  stats["total_entries"] = entries_.size();
  stats["avg_doc_length"] = avg_doc_length_;
  stats["total_tags"] = tag_index_.size();

  nlohmann::json dim_stats = nlohmann::json::object();
  for (const auto& [dim, values] : dimension_index_) {
    dim_stats[DimensionToString(dim)] = values.size();
  }
  stats["dimensions"] = dim_stats;

  return stats;
}

std::string StructuredMemory::GenerateId() const {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint64_t> dis;

  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
  return oss.str();
}

std::vector<std::string> StructuredMemory::Tokenize(const std::string& text) {
  std::vector<std::string> tokens;
  std::regex word_regex(R"(\w+)");

  auto words_begin = std::sregex_iterator(text.begin(), text.end(), word_regex);
  auto words_end = std::sregex_iterator();

  for (auto it = words_begin; it != words_end; ++it) {
    std::string token = it->str();
    std::transform(token.begin(), token.end(), token.begin(), ::tolower);
    tokens.push_back(token);
  }

  return tokens;
}

double StructuredMemory::ScoreBM25(const StructuredMemoryEntry& entry,
                                  const std::vector<std::string>& query_tokens) const {
  double score = 0.0;
  double doc_length = entry.tokens.size();

  for (const auto& query_token : query_tokens) {
    // 计算词频
    int tf = std::count(entry.tokens.begin(), entry.tokens.end(), query_token);
    if (tf == 0) continue;

    // 计算文档频率
    int df = DocumentFrequency(query_token);
    if (df == 0) continue;

    // IDF
    double idf = std::log((entries_.size() - df + 0.5) / (df + 0.5) + 1.0);

    // BM25 公式
    double numerator = tf * (kBM25_k1 + 1);
    double denominator = tf + kBM25_k1 * (1 - kBM25_b + kBM25_b * doc_length / avg_doc_length_);

    score += idf * numerator / denominator;
  }

  return score;
}

int StructuredMemory::DocumentFrequency(const std::string& term) const {
  int count = 0;
  for (const auto& [id, entry] : entries_) {
    if (std::find(entry.tokens.begin(), entry.tokens.end(), term) != entry.tokens.end()) {
      count++;
    }
  }
  return count;
}

}  // namespace ravbot
