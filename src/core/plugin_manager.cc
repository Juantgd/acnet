#include "plugin_manager.h"

#include <vector>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "helper.h"

namespace ac {

int PluginManager::ConfigLoader() {
  struct stat config_stat;
  if (stat(kConfigPath, &config_stat)) {
    LOG_E("Failed to load config file, error: {}", strerror(errno));
    return -1;
  }
  if (!S_ISREG(config_stat.st_mode)) {
    LOG_E("Failed to load config file, the file is not regular file.");
    return -1;
  }
  int fd = open(kConfigPath, O_RDONLY);
  if (fd < 0) {
    LOG_E("Failed to open config file, error: {}", strerror(errno));
    return -1;
  }
  std::vector<uint8_t> content(static_cast<size_t>(config_stat.st_size));
  if (read(fd, content.data(), static_cast<size_t>(config_stat.st_size)) <= 0) {
    LOG_E("Failed to read config file, error: {}", strerror(errno));
    return -1;
  }
  auto read_error = glz::read_toml(config_info_, content);
  if (read_error) {
    LOG_E("Failed to parse config file, error: {}",
          glz::format_error(read_error, content));
    return -1;
  }
  return 0;
}

} // namespace ac
