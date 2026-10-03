#pragma once

#include "config/config.hpp"

#include <iosfwd>

namespace coinbase_ticker_statistics {

/** Compose the live event loop; flush the output sink before reporting processing failures. */
[[nodiscard]] Result<void> run_application(const Config &config, std::ostream &diagnostics);

} // namespace coinbase_ticker_statistics
