add_library(coinbase_ticker_statistics_output
    csv_writer.cpp
    csv_sink.cpp
    format_fields.cpp
)

target_link_libraries(coinbase_ticker_statistics_output PUBLIC Boost::headers PRIVATE Threads::Threads)
target_include_directories(coinbase_ticker_statistics_output PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_output)
