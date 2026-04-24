// Copyright (c) 2026 juantgd. All Rights Reserved.

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

std::shared_ptr<ac_config> PluginManager::__config_loader() {
  struct stat config_stat;
  if (stat(kConfigPath, &config_stat)) {
    LOG_E("Failed to load config file, error: {}", strerror(errno));
    return nullptr;
  }
  if (!S_ISREG(config_stat.st_mode)) {
    LOG_E("Failed to load config file, the file is not regular file.");
    return nullptr;
  }
  int fd = open(kConfigPath, O_RDONLY);
  if (fd < 0) {
    LOG_E("Failed to open config file, error: {}", strerror(errno));
    return nullptr;
  }
  std::vector<uint8_t> content(static_cast<size_t>(config_stat.st_size));
  if (read(fd, content.data(), static_cast<size_t>(config_stat.st_size)) <= 0) {
    LOG_E("Failed to read config file, error: {}", strerror(errno));
    close(fd);
    return nullptr;
  }
  std::shared_ptr<ac_config> new_config = std::make_shared<ac_config>();
  auto read_error = glz::read_toml(*new_config, content);
  if (read_error) {
    LOG_E("Failed to parse config file, error: {}",
          glz::format_error(read_error, content));
    close(fd);
    return nullptr;
  }
  close(fd);
  return new_config;
}

bool PluginManager::UpdateConfig() {
  std::shared_ptr<ac_config> new_config = __config_loader();
  if (new_config) {
    config_info_ = std::move(new_config);
    return true;
  }
  LOG_E("failed to update config info.");
  return false;
}

} // namespace ac
