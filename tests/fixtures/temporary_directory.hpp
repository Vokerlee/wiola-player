#pragma once
#include <filesystem>
#include <random>
#include <stdexcept>

namespace wiola::testing {

// Each process/test owns a directory, including concurrent sanitizer configurations.
class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                ("wiola-test-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path_))
                return;
        }
        throw std::runtime_error{"Cannot create an isolated test directory"};
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace wiola::testing
