#pragma once

#include "app/logger.hpp"
#include <config/config.hpp>

namespace coinbase_ticker_statistics {

/** Run one asynchronous feed with bounded shutdown and final output cleanup.
 * Precondition: config was validated by load_config(), parse_and_validate_config() or validate_config()
 * and has not been changed since validation.
 */
[[nodiscard]] Result<void> run_application(const Config &config, Logger &logger);

} // namespace coinbase_ticker_statistics
