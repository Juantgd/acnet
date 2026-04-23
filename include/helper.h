// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_HELPER_H_
#define AC_INCLUDE_HELPER_H_

#include <atomic>
#include <new>
#include <utility>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#ifdef DEBUG_MODE
#include <quill/sinks/ConsoleSink.h>
#include <quill/std/Vector.h>
#else
#include <quill/sinks/FileSink.h>
#endif

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

class Logger {
public:
  static quill::Logger *Instance() {
    static Logger log;
    return log.logger_;
  }

  ~Logger() { quill::Backend::stop(); }

private:
  Logger() {
    quill::BackendOptions backend_options;
    backend_options.sleep_duration =
        std::chrono::microseconds(100);             // 无日志时sleep 10 ms
    backend_options.enable_yield_when_idle = false; // 关闭 yield
    quill::Backend::start(backend_options);
#ifdef DEBUG_MODE
    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1");
    logger_ =
        quill::Frontend::create_or_get_logger("root", std::move(console_sink));
    logger_->set_log_level(quill::LogLevel::TraceL3);
#else
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
        log_file,
        []() {
          quill::FileSinkConfig cfg;
          cfg.set_open_mode('w');
          cfg.set_filename_append_option(
              quill::FilenameAppendOption::StartDateTime);
          return cfg;
        }(),
        quill::FileEventNotifier{});

    logger_ =
        quill::Frontend::create_or_get_logger("root", std::move(file_sink));
    logger_->set_log_level(quill::LogLevel::Info);
#endif
  }
  constexpr static const char *log_file = "logs/acnet.log";
  quill::Logger *logger_;
};

#define LOG_D(fmt, ...) LOG_DEBUG(Logger::Instance(), fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) LOG_INFO(Logger::Instance(), fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) LOG_WARNING(Logger::Instance(), fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) LOG_ERROR(Logger::Instance(), fmt, ##__VA_ARGS__)

} // namespace ac

#endif
