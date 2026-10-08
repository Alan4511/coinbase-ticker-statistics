function(coinbase_ticker_statistics_enable_warnings target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "coinbase_ticker_statistics_enable_warnings: '${target}' is not a target")
    endif()

    target_compile_options("${target}" PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wcast-align
            -Wcast-qual
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Wold-style-cast
            -Woverloaded-virtual
            -Wnon-virtual-dtor
            -Wswitch-enum
            -Werror
        >
        # GCC's optional null-dereference heuristic reports false positives in
        # optimized Boost/std headers and valid test code; retain it on Clang.
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wnull-dereference>
        $<$<CXX_COMPILER_ID:GNU>:
            -Wlogical-op
            -Wduplicated-cond
            -Wduplicated-branches
            -Wmisleading-indentation
            -Wredundant-decls
            -Wundef
        >
    )
endfunction()
