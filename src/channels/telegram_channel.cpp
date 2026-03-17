// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ravbot/channels/telegram_channel.hpp"
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace ravbot {

namespace {

constexpr size_t kTelegramMessageChunkLimit = 4000;

// CURL write callback
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

bool telegram_response_ok(const nlohmann::json& response) {
    return response.contains("ok") && response["ok"].is_boolean() && response["ok"].get<bool>();
}

std::vector<std::string> split_telegram_message(const std::string& message) {
    if (message.empty()) {
        return {""};
    }

    std::vector<std::string> chunks;
    size_t start = 0;

    while (start < message.size()) {
        size_t end = std::min(start + kTelegramMessageChunkLimit, message.size());

        // 如果不是最后一块,尝试在句子边界分块
        if (end < message.size()) {
            // 优先在句号、问号、感叹号处分块
            size_t sentence_end = message.find_last_of(".?!\n", end);
            if (sentence_end != std::string::npos && sentence_end > start) {
                end = sentence_end + 1;
            } else {
                // 其次在空格处分块
                size_t space_pos = message.find_last_of(" \t\n", end);
                if (space_pos != std::string::npos && space_pos > start) {
                    end = space_pos + 1;
                } else {
                    // 最后确保不在 UTF-8 字符中间分块
                    while (end > start && (static_cast<unsigned char>(message[end]) & 0xC0) == 0x80) {
                        --end;
                    }
                    if (end == start) {
                        end = std::min(start + kTelegramMessageChunkLimit, message.size());
                    }
                }
            }
        }

        chunks.push_back(message.substr(start, end - start));
        start = end;
    }

    return chunks;
}

} // anonymous namespace

TelegramChannel::TelegramChannel(const TelegramConfig& config,
                                 std::shared_ptr<spdlog::logger> logger)
    : config_(config), logger_(logger) {
    LoadLastUpdateId();
}

TelegramChannel::~TelegramChannel() {
    Stop();
}

void TelegramChannel::Start() {
    if (running_) {
        logger_->warn("Telegram channel already running");
        return;
    }

    // Verify bot token and get bot info
    auto me = GetMe();
    if (me.contains("username")) {
        bot_username_ = me["username"].get<std::string>();
        logger_->info("Telegram bot started: @{}", bot_username_);
    } else {
        logger_->warn(
            "Failed to get bot info during startup; continuing without bot username until Telegram becomes reachable");
    }

    running_ = true;

    // 根据配置选择模式
    if (config_.mode == "webhook") {
        logger_->info("Telegram channel started in webhook mode");
        // Webhook 模式不需要轮询线程，由外部 WebServer 调用 HandleWebhookUpdate
    } else {
        // 默认使用轮询模式
        polling_thread_ = std::thread(&TelegramChannel::PollingLoop, this);
        logger_->info("Telegram channel started in polling mode");
    }
}

void TelegramChannel::Stop() {
    if (!running_) return;

    running_ = false;
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
    logger_->info("Telegram channel stopped");
}

void TelegramChannel::SendMessage(const std::string& chat_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    SendTextChunks(chat_id, message);
}

void TelegramChannel::SendReply(const std::string& chat_id, int message_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    SendTextChunks(chat_id, message, message_id);
}

void TelegramChannel::SendTextChunks(const std::string& chat_id,
                                     const std::string& message,
                                     std::optional<int> reply_to_message_id) {
    // 清理系统标签
    std::string sanitized_message = sanitizer_.SanitizeOutput(message);

    auto chunks = split_telegram_message(sanitized_message);

    // 如果只有一个分块,直接同步发送
    if (chunks.size() == 1) {
        nlohmann::json params = {
            {"chat_id", chat_id},
            {"text", chunks[0]}
        };
        if (reply_to_message_id.has_value()) {
            params["reply_to_message_id"] = *reply_to_message_id;
        }

        auto response = MakeApiRequest("sendMessage", params);
        if (!telegram_response_ok(response)) {
            logger_->error("Failed to send message to {}: {}", chat_id, response.dump());
        }
        return;
    }

    // 多个分块时使用线程化发送
    // 使用 shared_ptr 确保数据在异步操作中有效
    auto chunks_ptr = std::make_shared<std::vector<std::string>>(std::move(chunks));
    auto chat_id_ptr = std::make_shared<std::string>(chat_id);
    auto reply_id_ptr = std::make_shared<std::optional<int>>(reply_to_message_id);

    // 启动异步发送线程
    std::thread([this, chunks_ptr, chat_id_ptr, reply_id_ptr]() {
        for (size_t i = 0; i < chunks_ptr->size(); ++i) {
            nlohmann::json params = {
                {"chat_id", *chat_id_ptr},
                {"text", (*chunks_ptr)[i]}
            };

            // 只在第一个分块添加 reply_to
            if (reply_id_ptr->has_value() && i == 0) {
                params["reply_to_message_id"] = **reply_id_ptr;
            }

            auto response = MakeApiRequest("sendMessage", params);
            if (!telegram_response_ok(response)) {
                logger_->error("Failed to send message chunk {}/{} to {}: {}",
                               i + 1,
                               chunks_ptr->size(),
                               *chat_id_ptr,
                               response.dump());
                return;
            }

            // 添加小延迟避免 Telegram API 速率限制
            if (i < chunks_ptr->size() - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        logger_->debug("Sent {} message chunks to {}", chunks_ptr->size(), *chat_id_ptr);
    }).detach();
}

void TelegramChannel::SendPhoto(const std::string& chat_id, const std::string& photo_path, const std::string& caption) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    // 清理系统标签
    std::string sanitized_caption = sanitizer_.SanitizeOutput(caption);

    try {
        auto response = UploadFile("sendPhoto", chat_id, photo_path, "photo", sanitized_caption);
        if (!telegram_response_ok(response)) {
            logger_->error("Failed to send photo to {}: {}", chat_id, response.dump());
        }
    } catch (const std::exception& e) {
        logger_->error("Error sending photo: {}", e.what());
    }
}

void TelegramChannel::SendDocument(const std::string& chat_id, const std::string& file_path, const std::string& caption) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    // 清理系统标签
    std::string sanitized_caption = sanitizer_.SanitizeOutput(caption);

    try {
        auto response = UploadFile("sendDocument", chat_id, file_path, "document", sanitized_caption);
        if (!telegram_response_ok(response)) {
            logger_->error("Failed to send document to {}: {}", chat_id, response.dump());
        }
    } catch (const std::exception& e) {
        logger_->error("Error sending document: {}", e.what());
    }
}

void TelegramChannel::EditMessage(const std::string& chat_id, int message_id, const std::string& new_text) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    // 清理系统标签
    std::string sanitized_text = sanitizer_.SanitizeOutput(new_text);

    nlohmann::json params = {
        {"chat_id", chat_id},
        {"message_id", message_id},
        {"text", sanitized_text},
        {"parse_mode", "Markdown"}
    };

    MakeApiRequest("editMessageText", params);
}

void TelegramChannel::DeleteMessage(const std::string& chat_id, int message_id) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    nlohmann::json params = {
        {"chat_id", chat_id},
        {"message_id", message_id}
    };

    MakeApiRequest("deleteMessage", params);
}

void TelegramChannel::SendChatAction(const std::string& chat_id, const std::string& action) {
    std::lock_guard<std::mutex> lock(send_mutex_);

    nlohmann::json params = {
        {"chat_id", chat_id},
        {"action", action}
    };

    MakeApiRequest("sendChatAction", params);
}

nlohmann::json TelegramChannel::GetMe() {
    auto response = MakeApiRequest("getMe");
    if (response.contains("ok") && response["ok"].get<bool>() && response.contains("result")) {
        return response["result"];
    }
    return nlohmann::json::object();
}

nlohmann::json TelegramChannel::GetChat(const std::string& chat_id) {
    nlohmann::json params = {{"chat_id", chat_id}};
    auto response = MakeApiRequest("getChat", params);
    if (response.contains("ok") && response["ok"].get<bool>() && response.contains("result")) {
        return response["result"];
    }
    return nlohmann::json::object();
}

bool TelegramChannel::IsAllowed(const std::string& sender_id) const {
    if (config_.allow_from.empty() ||
        std::find(config_.allow_from.begin(), config_.allow_from.end(), "*") != config_.allow_from.end()) {
        return true;
    }

    return std::find(config_.allow_from.begin(), config_.allow_from.end(), sender_id) != config_.allow_from.end();
}

void TelegramChannel::PollingLoop() {
    logger_->info("Telegram polling loop started");

    while (running_) {
        try {
            if (bot_username_.empty()) {
                auto me = GetMe();
                if (me.contains("username")) {
                    bot_username_ = me["username"].get<std::string>();
                    logger_->info("Telegram bot connected: @{}", bot_username_);
                }
            }

            nlohmann::json params = {
                {"offset", last_update_id_ + 1},
                {"timeout", 30},
                {"allowed_updates", nlohmann::json::array({"message", "callback_query"})}
            };

            auto response = MakeApiRequest("getUpdates", params);

            if (response.contains("ok") && response["ok"].get<bool>() && response.contains("result")) {
                auto updates = response["result"];
                for (const auto& update : updates) {
                    try {
                        HandleUpdate(update);
                    } catch (const std::exception& e) {
                        logger_->error("Error handling Telegram update: {}", e.what());
                    }

                    if (update.contains("update_id")) {
                        last_update_id_ = update["update_id"].get<int64_t>();
                        SaveLastUpdateId();
                    }
                }
            } else {
                if (response.contains("description") && response["description"].is_string()) {
                    logger_->warn("Telegram getUpdates failed: {}",
                                  response["description"].get<std::string>());
                }
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        } catch (const std::exception& e) {
            logger_->error("Error in polling loop: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    logger_->info("Telegram polling loop stopped");
}

void TelegramChannel::HandleUpdate(const nlohmann::json& update) {
    // 提取 update_id
    // Requirements: 9.1, 9.3
    if (!update.contains("update_id")) {
        return;
    }
    int64_t update_id = update["update_id"].get<int64_t>();

    // 检查去重
    if (is_duplicate(update_id)) {
        logger_->debug("Skipping duplicate update: {}", update_id);
        return;
    }

    // 记录已处理
    record_update(update_id);

    // 处理消息
    if (update.contains("message")) {
        const auto& message = update["message"];

        // 提取 lane_id
        // Requirements: 9.4, 9.5
        std::string lane_id = get_lane_id(message);

        // 如果配置了 LaneProcessor，使用顺序处理
        if (lane_processor_) {
            lane_processor_->Submit(lane_id, [this, message]() {
                HandleMessage(message);
            });
        } else {
            // 直接处理
            HandleMessage(message);
        }
    } else if (update.contains("callback_query")) {
        HandleCallbackQuery(update["callback_query"]);
    }
}

void TelegramChannel::HandleMessage(const nlohmann::json& message) {
    if (!message.contains("text") || !message.contains("chat") || !message.contains("from")) {
        return;
    }

    std::string text = message["text"].get<std::string>();
    std::string chat_id = ExtractChatId(message["chat"]);
    std::string user_id = ExtractUserId(message["from"]);
    int message_id = message["message_id"].get<int>();

    // 提取 thread_id（如果有）
    // Requirements: 10.1, 10.2
    std::optional<std::string> thread_id = extract_thread_id(message);

    // 获取会话键
    std::string session_key;
    if (thread_binder_) {
        session_key = thread_binder_->GetSessionKey(chat_id, thread_id);
    } else {
        // 默认会话键
        session_key = "agent:main:telegram:" + chat_id;
        if (thread_id) {
            session_key += ":" + *thread_id;
        }
    }

    // Check if message should be processed
    if (!ShouldProcessMessage(message)) {
        return;
    }

    // Check permissions
    if (!IsAllowed(user_id)) {
        logger_->warn("User {} not allowed", user_id);
        return;
    }

    logger_->info("Received message from {} (session={}): {}", user_id,
                  session_key, text.substr(0, 50));

    // 启动持续的 "正在输入" 状态刷新
    // 使用 RAII 模式，函数结束时自动停止
    channels::TypingStateGuard typing_guard(chat_id, [this](const std::string& id) {
        this->SendChatAction(id, "typing");
    });

    // Forward to agent system via message handler
    if (message_handler_) {
        ChannelMessage msg;
        msg.id = std::to_string(message_id);
        msg.sender_id = user_id;
        msg.content = text;
        msg.channel_id = chat_id;
        msg.is_group_chat = IsGroupChat(message["chat"]);

        message_handler_(msg);
    } else {
        // Fallback: echo back if no handler is set
        logger_->warn("No message handler set, echoing back");
        SendMessage(chat_id, "Received: " + text);
    }
}

void TelegramChannel::HandleCallbackQuery(const nlohmann::json& callback_query) {
    // TODO: Implement callback query handling
    logger_->debug("Callback query received");
}

nlohmann::json TelegramChannel::MakeApiRequest(const std::string& method, const nlohmann::json& params) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        logger_->error("Failed to initialize CURL");
        return nlohmann::json::object();
    }

    std::string url = GetApiUrl(method);
    std::string response_data;
    std::string post_data = params.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        logger_->error("CURL error: {}", curl_easy_strerror(res));
        return nlohmann::json::object();
    }

    try {
        return nlohmann::json::parse(response_data);
    } catch (const std::exception& e) {
        logger_->error("Failed to parse response: {}", e.what());
        return nlohmann::json::object();
    }
}

std::string TelegramChannel::GetApiUrl(const std::string& method) const {
    return "https://api.telegram.org/bot" + config_.bot_token + "/" + method;
}

std::string TelegramChannel::GetFileUrl(const std::string& file_path) const {
    return "https://api.telegram.org/file/bot" + config_.bot_token + "/" + file_path;
}

bool TelegramChannel::CheckConnection() {
    auto me = GetMe();
    return !me.empty();
}

bool TelegramChannel::IsGroupChat(const nlohmann::json& chat) const {
    if (!chat.contains("type")) return false;
    std::string type = chat["type"].get<std::string>();
    return type == "group" || type == "supergroup";
}

bool TelegramChannel::ShouldProcessMessage(const nlohmann::json& message) const {
    if (!message.contains("chat") || !message.contains("text")) {
        return false;
    }

    bool is_group = IsGroupChat(message["chat"]);
    std::string text = message["text"].get<std::string>();

    // Check policy
    if (is_group && config_.group_policy == "closed") {
        return false;
    }
    if (!is_group && config_.dm_policy == "closed") {
        return false;
    }

    // Check mention requirement in groups
    if (is_group && config_.require_mention) {
        std::string mention = "@" + bot_username_;
        if (text.find(mention) == std::string::npos) {
            return false;
        }
    }

    return true;
}

std::string TelegramChannel::ExtractChatId(const nlohmann::json& chat) const {
    if (chat.contains("id")) {
        return std::to_string(chat["id"].get<int64_t>());
    }
    return "";
}

std::string TelegramChannel::ExtractUserId(const nlohmann::json& user) const {
    if (user.contains("id")) {
        return std::to_string(user["id"].get<int64_t>());
    }
    return "";
}

void TelegramChannel::LoadLastUpdateId() {
    std::string state_file = std::string(std::getenv("HOME")) + "/.ravbot/telegram_state.json";
    try {
        if (std::filesystem::exists(state_file)) {
            std::ifstream file(state_file);
            nlohmann::json state;
            file >> state;
            if (state.contains("last_update_id")) {
                last_update_id_ = state["last_update_id"].get<int64_t>();
                logger_->info("Loaded last_update_id: {}", last_update_id_);
            }
        }
    } catch (const std::exception& e) {
        logger_->warn("Failed to load last_update_id: {}", e.what());
    }
}

void TelegramChannel::SaveLastUpdateId() {
    std::string state_file = std::string(std::getenv("HOME")) + "/.ravbot/telegram_state.json";
    try {
        nlohmann::json state;
        state["last_update_id"] = last_update_id_;
        std::ofstream file(state_file);
        file << state.dump(2);
        logger_->debug("Saved last_update_id: {}", last_update_id_);
    } catch (const std::exception& e) {
        logger_->warn("Failed to save last_update_id: {}", e.what());
    }
}

// Set deduplication store
// Requirements: 9.1, 9.2, 9.3
void TelegramChannel::SetDeduplicationStore(
    std::shared_ptr<DeduplicationStore> store) {
  dedup_store_ = std::move(store);
  logger_->info("DeduplicationStore configured");
}

// Set lane processor
// Requirements: 9.4, 9.5, 9.6
void TelegramChannel::SetLaneProcessor(
    std::shared_ptr<LaneProcessor> processor) {
  lane_processor_ = std::move(processor);
  logger_->info("LaneProcessor configured");
}

// Set thread binder
// Requirements: 10.1, 10.2, 10.3
void TelegramChannel::SetThreadBinder(std::shared_ptr<ThreadBinder> binder) {
  thread_binder_ = std::move(binder);
  logger_->info("ThreadBinder configured");
}

// Check if update is duplicate
// Requirements: 9.1, 9.3
bool TelegramChannel::is_duplicate(int64_t update_id) const {
  if (dedup_store_) {
    return dedup_store_->IsProcessed(update_id);
  }
  // Fallback: 如果没有配置 dedup_store，认为不重复
  return false;
}

// Record processed update
// Requirements: 9.1, 9.2
void TelegramChannel::record_update(int64_t update_id) {
  if (dedup_store_) {
    dedup_store_->MarkProcessed(update_id);
  }
}

// Get lane ID for message
// Requirements: 9.4
std::string TelegramChannel::get_lane_id(const nlohmann::json& message) const {
  // 使用 chat_id 作为 lane_id，确保同一聊天的消息顺序处理
  if (message.contains("chat")) {
    return ExtractChatId(message["chat"]);
  }
  return "default";
}

// Extract thread ID from message
// Requirements: 10.1
std::optional<std::string> TelegramChannel::extract_thread_id(
    const nlohmann::json& message) const {
  // Telegram 的 topic/thread 支持
  if (message.contains("message_thread_id")) {
    return std::to_string(message["message_thread_id"].get<int64_t>());
  }
  return std::nullopt;
}

// Webhook 方法实现
bool TelegramChannel::SetWebhook(const std::string& url, const std::string& secret_token) {
    nlohmann::json params = {
        {"url", url},
        {"allowed_updates", nlohmann::json::array({"message", "callback_query"})}
    };

    if (!secret_token.empty()) {
        params["secret_token"] = secret_token;
    }

    auto response = MakeApiRequest("setWebhook", params);

    if (telegram_response_ok(response)) {
        logger_->info("Webhook set successfully: {}", url);
        return true;
    } else {
        logger_->error("Failed to set webhook: {}", response.dump());
        return false;
    }
}

bool TelegramChannel::DeleteWebhook() {
    auto response = MakeApiRequest("deleteWebhook");

    if (telegram_response_ok(response)) {
        logger_->info("Webhook deleted successfully");
        return true;
    } else {
        logger_->error("Failed to delete webhook: {}", response.dump());
        return false;
    }
}

nlohmann::json TelegramChannel::GetWebhookInfo() {
    auto response = MakeApiRequest("getWebhookInfo");

    if (telegram_response_ok(response) && response.contains("result")) {
        return response["result"];
    }

    return nlohmann::json::object();
}

void TelegramChannel::HandleWebhookUpdate(const nlohmann::json& update, const std::string& secret_token) {
    // 验证 secret token（如果配置了）
    if (!config_.webhook_secret.empty() && secret_token != config_.webhook_secret) {
        logger_->warn("Webhook update rejected: invalid secret token");
        return;
    }

    // 处理更新（与轮询模式相同的逻辑）
    try {
        HandleUpdate(update);

        // 保存 update_id（用于故障恢复）
        if (update.contains("update_id")) {
            last_update_id_ = update["update_id"].get<int64_t>();
            SaveLastUpdateId();
        }
    } catch (const std::exception& e) {
        logger_->error("Error handling webhook update: {}", e.what());
    }
}

// 媒体处理方法实现
nlohmann::json TelegramChannel::GetFile(const std::string& file_id) {
    nlohmann::json params = {{"file_id", file_id}};
    auto response = MakeApiRequest("getFile", params);

    if (telegram_response_ok(response) && response.contains("result")) {
        return response["result"];
    }

    return nlohmann::json::object();
}

std::string TelegramChannel::DownloadFile(const std::string& file_id, const std::string& save_path) {
    // 获取文件信息
    auto file_info = GetFile(file_id);
    if (!file_info.contains("file_path")) {
        logger_->error("Failed to get file path for file_id: {}", file_id);
        return "";
    }

    std::string file_path = file_info["file_path"].get<std::string>();
    std::string file_url = GetFileUrl(file_path);

    // 下载文件内容
    std::string content = DownloadFileContent(file_url);
    if (content.empty()) {
        logger_->error("Failed to download file from: {}", file_url);
        return "";
    }

    // 如果指定了保存路径，保存到文件
    if (!save_path.empty()) {
        try {
            std::ofstream file(save_path, std::ios::binary);
            if (!file) {
                logger_->error("Failed to open file for writing: {}", save_path);
                return "";
            }
            file.write(content.data(), content.size());
            file.close();
            logger_->info("File downloaded successfully: {}", save_path);
            return save_path;
        } catch (const std::exception& e) {
            logger_->error("Error saving file: {}", e.what());
            return "";
        }
    }

    return content;
}

nlohmann::json TelegramChannel::UploadFile(const std::string& method,
                                           const std::string& chat_id,
                                           const std::string& file_path,
                                           const std::string& file_field,
                                           const std::string& caption) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        logger_->error("Failed to initialize CURL");
        return nlohmann::json::object();
    }

    std::string url = GetApiUrl(method);
    std::string response_data;

    // 读取文件内容
    std::string file_content = ReadFileContent(file_path);
    if (file_content.empty()) {
        curl_easy_cleanup(curl);
        return nlohmann::json::object();
    }

    // 提取文件名
    std::string filename = file_path;
    size_t last_slash = file_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        filename = file_path.substr(last_slash + 1);
    }

    // 构建 multipart/form-data
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part;

    // 添加 chat_id 字段
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "chat_id");
    curl_mime_data(part, chat_id.c_str(), CURL_ZERO_TERMINATED);

    // 添加文件字段
    part = curl_mime_addpart(mime);
    curl_mime_name(part, file_field.c_str());
    curl_mime_data(part, file_content.c_str(), file_content.size());
    curl_mime_filename(part, filename.c_str());

    // 添加 caption 字段（如果有）
    if (!caption.empty()) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "caption");
        curl_mime_data(part, caption.c_str(), CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        logger_->error("CURL error: {}", curl_easy_strerror(res));
        return nlohmann::json::object();
    }

    try {
        return nlohmann::json::parse(response_data);
    } catch (const std::exception& e) {
        logger_->error("Failed to parse response: {}", e.what());
        return nlohmann::json::object();
    }
}

std::string TelegramChannel::ReadFileContent(const std::string& file_path) {
    try {
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            logger_->error("Failed to open file: {}", file_path);
            return "";
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    } catch (const std::exception& e) {
        logger_->error("Error reading file: {}", e.what());
        return "";
    }
}

std::string TelegramChannel::DownloadFileContent(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        logger_->error("Failed to initialize CURL");
        return "";
    }

    std::string response_data;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        logger_->error("CURL error: {}", curl_easy_strerror(res));
        return "";
    }

    return response_data;
}

} // namespace ravbot
