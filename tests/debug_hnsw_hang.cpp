// Test to isolate HNSW search hang issue
#include "ravbot/core/vector_database.hpp"
#include <iostream>
#include <vector>

using namespace ravbot;

int main() {
    auto logger = spdlog::default_logger();
    logger->set_level(spdlog::level::debug);

    VectorDatabaseConfig config;
    config.use_hnsw = true;

    VectorDatabase db(":memory:", logger, config);
    if (!db.Initialize()) {
        std::cerr << "Failed to initialize database" << std::endl;
        return 1;
    }

    std::cout << "Database initialized" << std::endl;

    // Add vectors with diverse patterns
    std::vector<float> vec1(128);
    std::vector<float> vec2(128);
    std::vector<float> vec3(128);

    for (int i = 0; i < 128; ++i) {
        vec1[i] = static_cast<float>(i) / 128.0f;
        vec2[i] = static_cast<float>(128 - i) / 128.0f;
        vec3[i] = (i % 2 == 0) ? 1.0f : 0.0f;
    }

    std::cout << "Adding vectors..." << std::endl;
    std::cout << "Adding doc1..." << std::endl;
    db.IndexVector("doc1", vec1, "text1");
    std::cout << "Added doc1" << std::endl;

    std::cout << "Adding doc2..." << std::endl;
    db.IndexVector("doc2", vec2, "text2");
    std::cout << "Added doc2" << std::endl;

    std::cout << "Adding doc3..." << std::endl;
    db.IndexVector("doc3", vec3, "text3");
    std::cout << "Added doc3" << std::endl;

    std::cout << "Vectors added, count: " << db.GetVectorCount() << std::endl;

    // Search
    std::cout << "Starting search..." << std::endl;
    auto results = db.SearchVectors(vec1, 3, 0.0f);

    std::cout << "Search completed, results: " << results.size() << std::endl;

    return 0;
}
