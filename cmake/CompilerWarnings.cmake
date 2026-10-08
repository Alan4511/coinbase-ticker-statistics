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
            -Wnull-dereference
            -Wswitch-enum
            -Werror
        >
        $<$<CXX_COMPILER_ID:GNU>:
            # Optimized Boost headers trigger this diagnostic even as system includes.
            # Retain the warning without promoting it to an error; other warnings remain fatal.
            -Wno-error=null-dereference
            -Wlogical-op
            -Wduplicated-cond
            -Wduplicated-branches
            -Wmisleading-indentation
            -Wredundant-decls
            -Wundef
        >
    )
endfunction()
