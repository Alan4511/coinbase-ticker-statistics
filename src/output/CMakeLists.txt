add_library(coinbase_ticker_statistics_output
    csv_writer.cpp
    format_fields.cpp
)


target_include_directories(coinbase_ticker_statistics_output PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

coinbase_ticker_statistics_enable_warnings(coinbase_ticker_statistics_output)
