# Build setup

## Requirements

- CMake 3.20 or later.
- A C++23 compiler and standard library supporting `std::expected`, including
  monadic operations. Use GCC 14+, Clang 19+ with a compatible standard library,
  or AppleClang 16+.
- OpenSSL 3 development files.

CMake uses installed Boost 1.83+ or fetches pinned Boost 1.88.0 headers,
nlohmann-json 3.12.0 and GoogleTest 1.18.0. The first configuration needs network
access to fetch dependencies.

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
