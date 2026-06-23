#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <string>
#include <memory>

#include "spdlog/spdlog.h"

class Logger {
  public:
    static void Init(const std::string& log_dir, const std::string& log_name, bool is_log_rate_day, size_t max_size,
                     int log_file_num);

    static std::shared_ptr<spdlog::logger> Instance();

    static void Shutdown();

  private:
    Logger() = default;
    static std::shared_ptr<spdlog::logger> g_logger;
};