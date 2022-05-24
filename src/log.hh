#ifndef HGDB_LOG_HH
#define HGDB_LOG_HH

#include <string_view>

namespace hgdb::log {
enum class log_level { info, error };

void log(log_level level, std::string_view msg);

}  // namespace hgdb::log

#endif  // HGDB_LOG_HH
