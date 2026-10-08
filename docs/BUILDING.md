# Build setup

## Requirements

- CMake 3.20 or later.
- A C++23 compiler and standard library supporting `std::expected`, including
  monadic operations, and chrono formatting with `std::format`.
  Use GCC 14+, Clang 19+ with a compatible standard library,
  or AppleClang 16+.
- OpenSSL 3 development files.

CMake uses installed Boost 1.88+ or fetches pinned Boost 1.88.0, and fetches
Glaze 9.0.0 headers, and GoogleTest 1.18.0 for tests. Glaze is consumed as a
header-only dependency, so its upstream CMake minimum does not apply. The first
configuration needs network access to fetch dependencies.

## Platform setup

On Debian, install the compiler, CMake and `libssl-dev`.

On macOS, install `openssl@3` with Homebrew. If CMake cannot find it, configure with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix openssl@3)"
```

## Optional build settings

Tests are enabled by default. To build only the application, add
`-DCOINBASE_TICKER_STATISTICS_BUILD_TESTS=OFF` when configuring.

For sanitizer builds, use a separate directory:

```sh
cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DCOINBASE_TICKER_STATISTICS_SANITIZERS=ON
cmake --build build/sanitized --parallel
ctest --test-dir build/sanitized --output-on-failure
```

The default sanitizer set is `address,undefined`. Select UBSan alone with
`-DCOINBASE_TICKER_STATISTICS_SANITIZER_SET=undefined`.

## CI verification

- [GitHub Actions](../.github/workflows/ci.yml) runs on pushes, pull requests and manual dispatch.
- GCC/Clang Release and Clang Debug ASan/UBSan jobs build and test in Debian on GitHub-hosted Linux runners.
- Each job runs local TLS tests and the independent CSV fixture verifier, retaining reports for seven days.
