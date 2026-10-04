#pragma once

#include "app/logger.hpp"
#include <config/config.hpp>

namespace coinbase_ticker_statistics {

/** Run one asynchronous feed; signals request bounded close, final flushing and diagnostics. */
[[nodiscard]] Result<void> run_application(const Config &config, Logger &logger);

} // namespace coinbase_ticker_statistics
