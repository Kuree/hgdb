#ifndef HGDB_LOG_HH
#define HGDB_LOG_HH

#include <string>

namespace hgdb::log {
enum class log_level {
    info,
    error
};

void log(log_level level, const std::string &msg);

}

#endif  // HGDB_LOG_HH
