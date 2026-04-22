// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_PLUGIN_MANAGER_H_
#define AC_INCLUDE_PLUGIN_MANAGER_H_

#include <string>
#include <vector>

#include <glaze/toml.hpp>

#include "helper.h"

namespace ac {

namespace {
constexpr const char *kConfigPath = "config/server.toml";
}

struct ac_module {
  std::string module_name;
  std::string library_name;
};

struct ac_config {
  std::string library_path;
  std::vector<ac_module> modules;
};

class PluginManager {
public:
  ~PluginManager() = default;

  inline static PluginManager &Instance() {
    static PluginManager plug_manager;
    return plug_manager;
  }

  ac_config &GetConfigInfo() { return config_info_; }

  void UpdateConfig();

private:
  PluginManager() {
    if (!__config_loader()) {
      LOG_E("failed to loaded config file.");
      std::terminate();
    }
  };

  bool __config_loader();
  ac_config config_info_;
};

} // namespace ac

template <> struct glz::meta<ac::ac_module> {
  using T = ac::ac_module;
  static constexpr auto value = object(&T::module_name, &T::library_name);
};

template <> struct glz::meta<ac::ac_config> {
  using T = ac::ac_config;
  static constexpr auto value = object(&T::library_path, &T::modules);
};

#endif
