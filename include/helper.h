#ifndef AC_INCLUDE_HELPER_H_
#define AC_INCLUDE_HELPER_H_

#include <atomic>
#include <cstdio>
#include <ctime>
#include <format>

namespace ac {

// 缓存行大小
#ifdef __cpp_lib_hardware_interference_size
constexpr std::size_t kCacheLineSize =
    std::hardware_constructive_interference_size;
#else
constexpr std::size_t kCacheLineSize = 64;
#endif

inline std::size_t generate_unique_id() {
  static std::atomic<std::size_t> __count{1};
  return __count.fetch_add(1, std::memory_order_relaxed);
}

inline thread_local char tls_timebuf[24]{0};

inline const char *gettimestamp() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm t_info;
  localtime_r(&ts.tv_sec, &t_info);
  strftime(tls_timebuf, 20, "%Y-%m-%d %H:%M:%S", &t_info);
  snprintf(tls_timebuf + 19, 5, ".%d", static_cast<int>(ts.tv_nsec / 1000000));
  return tls_timebuf;
}

#define LOG_D(fmt, ...)                                                        \
  fprintf(stdout, "[%s] [DEBUG] [%s:%d] %s\n", gettimestamp(), __FILE__,       \
          __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())

#define LOG_I(fmt, ...)                                                        \
  fprintf(stdout, "[%s] [INFO] [%s:%d] %s\n", gettimestamp(), __FILE__,        \
          __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())

#define LOG_W(fmt, ...)                                                        \
  fprintf(stderr, "[%s] [WARN] [%s:%d] %s\n", gettimestamp(), __FILE__,        \
          __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())

#define LOG_E(fmt, ...)                                                        \
  fprintf(stderr, "[%s] [ERROR] [%s:%d] %s\n", gettimestamp(), __FILE__,       \
          __LINE__, std::format(fmt, ##__VA_ARGS__).c_str())

} // namespace ac

#endif
