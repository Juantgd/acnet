#ifndef AC_INCLUDE_PLUGIN_MANAGER_H_
#define AC_INCLUDE_PLUGIN_MANAGER_H_

#include <string>
#include <vector>

#include <glaze/toml.hpp>

namespace ac {

namespace {
constexpr const char *kConfigPath = "config/server.toml";
}

struct ac_module {
  std::string module_name;
  std::string module_version;
  std::string module_path;
};

struct ac_config {
  std::string host;
  short port;
  std::vector<ac_module> modules;
};

class PluginManager {
public:
  PluginManager() = default;
  ~PluginManager() = default;

  int ConfigLoader();

private:
  ac_config config_info_;
};

} // namespace ac

template <> struct glz::meta<ac::ac_module> {
  using T = ac::ac_module;
  static constexpr auto value =
      object(&T::module_name, &T::module_version, &T::module_path);
};

template <> struct glz::meta<ac::ac_config> {
  using T = ac::ac_config;
  static constexpr auto value = object(&T::host, &T::port, &T::modules);
};

#endif
