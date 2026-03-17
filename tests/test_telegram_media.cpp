// Copyright 2025 RavBot Contributors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include "ravbot/channels/telegram_channel.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using namespace ravbot;

// Mock TelegramChannel for testing
class MockTelegramMediaChannel : public TelegramChannel {
public:
    MockTelegramMediaChannel(const TelegramConfig& config, std::shared_ptr<spdlog::logger> logger)
        : TelegramChannel(config, logger) {}

    // Override MakeApiRequest to avoid real API calls
    nlohmann::json MakeApiRequest(const std::string& method, const nlohmann::json& params = {}) override {
        last_method_ = method;
        last_params_ = params;

        // Mock responses
        if (method == "getFile") {
            return {
                {"ok", true},
                {"result", {
                    {"file_id", "test-file-id"},
                    {"file_unique_id", "test-unique-id"},
                    {"file_size", 1024},
                    {"file_path", "photos/test.jpg"}
                }}
            };
        } else if (method == "sendPhoto" || method == "sendDocument") {
            return {
                {"ok", true},
                {"result", {
                    {"message_id", 123},
                    {"chat", {{"id", 987654321}}},
                    {"date", 1234567890}
                }}
            };
        } else if (method == "getMe") {
            return {
                {"ok", true},
                {"result", {
                    {"id", 123456789},
                    {"is_bot", true},
                    {"first_name", "TestBot"},
                    {"username", "test_bot"}
                }}
            };
        }

        return {{"ok", false}};
    }

    std::string last_method_;
    nlohmann::json last_params_;
};

class TelegramMediaTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = spdlog::default_logger();

        config_.bot_token = "test-token";
        config_.mode = "polling";

        // 创建测试文件
        test_file_path_ = "/tmp/test_telegram_file.txt";
        test_image_path_ = "/tmp/test_telegram_image.jpg";

        std::ofstream file(test_file_path_);
        file << "Test file content";
        file.close();

        std::ofstream image(test_image_path_, std::ios::binary);
        // 写入一个最小的 JPEG 文件头
        unsigned char jpeg_header[] = {
            0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46,
            0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
            0x00, 0x01, 0x00, 0x00, 0xFF, 0xD9
        };
        image.write(reinterpret_cast<char*>(jpeg_header), sizeof(jpeg_header));
        image.close();
    }

    void TearDown() override {
        // 清理测试文件
        if (std::filesystem::exists(test_file_path_)) {
            std::filesystem::remove(test_file_path_);
        }
        if (std::filesystem::exists(test_image_path_)) {
            std::filesystem::remove(test_image_path_);
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
    TelegramConfig config_;
    std::string test_file_path_;
    std::string test_image_path_;
};

// 测试 GetFile
TEST_F(TelegramMediaTest, GetFile) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    auto file_info = channel->GetFile("test-file-id");

    EXPECT_EQ(channel->last_method_, "getFile");
    EXPECT_TRUE(channel->last_params_.contains("file_id"));
    EXPECT_EQ(channel->last_params_["file_id"], "test-file-id");

    EXPECT_TRUE(file_info.contains("file_id"));
    EXPECT_TRUE(file_info.contains("file_path"));
    EXPECT_EQ(file_info["file_id"], "test-file-id");
    EXPECT_EQ(file_info["file_path"], "photos/test.jpg");
}

// 测试 SendPhoto
TEST_F(TelegramMediaTest, SendPhoto) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", test_image_path_, "Test caption");
    });
}

// 测试 SendPhoto 带 caption
TEST_F(TelegramMediaTest, SendPhotoWithCaption) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", test_image_path_, "This is a test image");
    });
}

// 测试 SendDocument
TEST_F(TelegramMediaTest, SendDocument) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendDocument("123456", test_file_path_, "Test document");
    });
}

// 测试 SendDocument 不带 caption
TEST_F(TelegramMediaTest, SendDocumentWithoutCaption) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendDocument("123456", test_file_path_);
    });
}

// 测试文件不存在
TEST_F(TelegramMediaTest, SendPhotoFileNotFound) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", "/nonexistent/file.jpg");
    });
}

// 测试空文件路径
TEST_F(TelegramMediaTest, SendPhotoEmptyPath) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", "");
    });
}

// 测试大文件
TEST_F(TelegramMediaTest, SendLargeFile) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    // 创建一个较大的测试文件 (1MB)
    std::string large_file_path = "/tmp/test_large_file.bin";
    std::ofstream large_file(large_file_path, std::ios::binary);
    std::vector<char> data(1024 * 1024, 'A');
    large_file.write(data.data(), data.size());
    large_file.close();

    EXPECT_NO_THROW({
        channel->SendDocument("123456", large_file_path);
    });

    // 清理
    if (std::filesystem::exists(large_file_path)) {
        std::filesystem::remove(large_file_path);
    }
}

// 测试多种文件类型
TEST_F(TelegramMediaTest, SendDifferentFileTypes) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    // PDF
    std::string pdf_path = "/tmp/test.pdf";
    std::ofstream pdf(pdf_path);
    pdf << "%PDF-1.4\n";
    pdf.close();

    EXPECT_NO_THROW({
        channel->SendDocument("123456", pdf_path);
    });

    // ZIP
    std::string zip_path = "/tmp/test.zip";
    std::ofstream zip(zip_path, std::ios::binary);
    unsigned char zip_header[] = {0x50, 0x4B, 0x03, 0x04};
    zip.write(reinterpret_cast<char*>(zip_header), sizeof(zip_header));
    zip.close();

    EXPECT_NO_THROW({
        channel->SendDocument("123456", zip_path);
    });

    // 清理
    if (std::filesystem::exists(pdf_path)) {
        std::filesystem::remove(pdf_path);
    }
    if (std::filesystem::exists(zip_path)) {
        std::filesystem::remove(zip_path);
    }
}

// 测试并发发送
TEST_F(TelegramMediaTest, ConcurrentSend) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&channel, this, i]() {
            channel->SendPhoto("123456", test_image_path_, "Image " + std::to_string(i));
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

// 测试文件名提取
TEST_F(TelegramMediaTest, FilenameExtraction) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    // Unix 路径
    EXPECT_NO_THROW({
        channel->SendDocument("123456", "/path/to/file.txt");
    });

    // Windows 路径
    EXPECT_NO_THROW({
        channel->SendDocument("123456", "C:\\path\\to\\file.txt");
    });

    // 相对路径
    EXPECT_NO_THROW({
        channel->SendDocument("123456", "file.txt");
    });
}

// 测试特殊字符文件名
TEST_F(TelegramMediaTest, SpecialCharactersInFilename) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    std::string special_file = "/tmp/test file with spaces.txt";
    std::ofstream file(special_file);
    file << "Test content";
    file.close();

    EXPECT_NO_THROW({
        channel->SendDocument("123456", special_file);
    });

    // 清理
    if (std::filesystem::exists(special_file)) {
        std::filesystem::remove(special_file);
    }
}

// 测试 UTF-8 文件名
TEST_F(TelegramMediaTest, UTF8Filename) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    std::string utf8_file = "/tmp/测试文件.txt";
    std::ofstream file(utf8_file);
    file << "Test content";
    file.close();

    EXPECT_NO_THROW({
        channel->SendDocument("123456", utf8_file);
    });

    // 清理
    if (std::filesystem::exists(utf8_file)) {
        std::filesystem::remove(utf8_file);
    }
}

// 测试空 caption
TEST_F(TelegramMediaTest, EmptyCaption) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", test_image_path_, "");
    });
}

// 测试长 caption
TEST_F(TelegramMediaTest, LongCaption) {
    auto channel = std::make_unique<MockTelegramMediaChannel>(config_, logger_);

    std::string long_caption(1000, 'A');

    EXPECT_NO_THROW({
        channel->SendPhoto("123456", test_image_path_, long_caption);
    });
}
