#pragma once

#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <system_error>
#include <utility>

namespace coinbase_ticker_statistics::test {

/** Call only in an isolated child: make regular-file output fail at a known size. */
inline bool limit_child_file_size(std::size_t maximum_bytes) {
    rlimit limit{};
    if (!std::in_range<rlim_t>(maximum_bytes) || ::getrlimit(RLIMIT_FSIZE, &limit) != 0)
        return false;
    limit.rlim_cur = static_cast<rlim_t>(maximum_bytes);
    return std::signal(SIGXFSZ, SIG_IGN) != SIG_ERR && ::setrlimit(RLIMIT_FSIZE, &limit) == 0;
}

/** Own an isolated directory. A setup failure aborts the test before it can write elsewhere. */
class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::random_device random;
        constexpr unsigned maximum_attempts = 100;
        for (unsigned attempt = 0; attempt < maximum_attempts; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                             ("ticker-test-" + std::to_string(random()) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot create isolated test directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    [[nodiscard]] std::filesystem::path file(std::string_view name) const {
        return path_ / name;
    }

  private:
    std::filesystem::path path_;
};

inline std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot read test file: " + path.string());
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

inline void write_file(const std::filesystem::path &path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    stream.close();
    if (!stream)
        throw std::runtime_error("cannot write test file: " + path.string());
}

} // namespace coinbase_ticker_statistics::test
