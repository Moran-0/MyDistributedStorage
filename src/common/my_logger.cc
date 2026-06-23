
#include "my_logger.h"

#include <iostream>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if __cplusplus >= 201703L
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

std::shared_ptr<spdlog::logger> Logger::g_logger = nullptr;

void Logger::Init(const std::string& log_dir, const std::string& log_name, bool is_log_rate_day, size_t max_size,
                  int log_file_num) {
    std::string log_path;

    try {
        if (!fs::exists(log_dir)) {
            fs::create_directories(log_dir);
        }

        log_path = log_dir + "/" + log_name;

        // 初始化异步线程池
        spdlog::init_thread_pool(8192, 1);

        // 创建文件日志接收器（根据轮转方式选择）
        std::shared_ptr<spdlog::sinks::sink> file_sink;
        if (is_log_rate_day) {
            file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_path, 0, 0);
        } else {
            file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_path, max_size, log_file_num, false);
        }

        // 创建控制台日志接收器（带颜色输出）
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // 组合文件和控制台接收器（多接收器）
        std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
        sinks.push_back(file_sink);    // 添加文件接收器
        sinks.push_back(console_sink); // 添加控制台接收器

        // 使用多接收器创建异步日志器
        g_logger = std::make_shared<spdlog::async_logger>(log_name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block);

        // 设置日志格式（同时作用于文件和控制台）
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [tid %t] [%^%l%$] %v");
        // 设置日志打印等级
        g_logger->set_level(spdlog::level::debug);

        spdlog::set_default_logger(g_logger);
        g_logger->info("start log (output to file and console)");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger Init failed: " << ex.what() << "\n";
        std::cerr << "Fallback to /tmp" << std::endl;

        // 降级方案同样支持控制台输出
        log_path = "/tmp/" + log_name;
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_path, max_size, log_file_num);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
        sinks.push_back(file_sink);
        sinks.push_back(console_sink);

        g_logger = std::make_shared<spdlog::async_logger>(log_name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
        g_logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(g_logger);
    }
}

std::shared_ptr<spdlog::logger> Logger::Instance() {
    return g_logger;
}

void Logger::Shutdown() {
    if (g_logger) {
        spdlog::drop_all();
        g_logger = nullptr;
    }
}