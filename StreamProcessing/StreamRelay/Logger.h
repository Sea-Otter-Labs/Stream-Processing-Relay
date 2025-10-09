#pragma once

#include <memory>
#include <string>
#include <ctime>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/file_helper.h>

// ---------------- 自定义 sink ----------------
template<typename Mutex>
class daily_rotating_file_sink : public spdlog::sinks::base_sink<Mutex>
{
public:
    daily_rotating_file_sink(const std::string &base_filename,
                             size_t max_size,
                             size_t max_files)
        : base_filename_(base_filename),
          max_size_(max_size),
          max_files_(max_files)
    {
        current_date_ = get_current_date();
        open_new_file();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        auto today = get_current_date();
        if (today != current_date_)
        {
            current_date_ = today;
            open_new_file();
        }

        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        file_helper_.write(formatted);
        current_size_ += formatted.size();

        if (current_size_ >= max_size_)
        {
            rotate_files_();
        }
    }

    void flush_() override
    {
        file_helper_.flush();
    }

private:
    std::string base_filename_;   // 基础文件名
    std::string current_date_;    // 当前日期
    size_t max_size_;             // 单文件最大大小
    size_t max_files_;            // 保留的文件数
    size_t current_size_{0};      // 当前文件大小

    spdlog::details::file_helper file_helper_;

    static std::string get_current_date()
    {
        auto t = std::time(nullptr);
        std::tm tm_time{};
        localtime_r(&t, &tm_time);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_time);
        return buf;
    }

    void open_new_file()
    {
        std::string filename = base_filename_ + "_" + current_date_ + ".log";
        file_helper_.open(filename, true);
        current_size_ = file_helper_.size();
    }

    void rotate_files_()
    {
        file_helper_.close();

        for (size_t i = max_files_; i > 0; --i)
        {
            std::string src = base_filename_ + "_" + current_date_ +
                              (i == 1 ? ".log" : ("." + std::to_string(i-1) + ".log"));
            std::string target = base_filename_ + "_" + current_date_ +
                                 "." + std::to_string(i) + ".log";

            if (std::filesystem::exists(src))
            {
                std::error_code ec;
                std::filesystem::rename(src, target, ec);
            }
        }

        open_new_file();
    }
};

using daily_rotating_file_sink_mt = daily_rotating_file_sink<std::mutex>;

// ---------------- Logger 类 ----------------
class Logger
{
public:
    enum class Level { Debug, Info, Warn, Error };

    // 获取全局日志实例
    static std::shared_ptr<spdlog::logger>& getInstance()
    {
        static std::shared_ptr<spdlog::logger> instance = nullptr;
        return instance;
    }

    // 初始化日志
    static void init(Level level = Level::Info,
                     const std::string& logDir = "./logs",
                     size_t maxFileSize = 10 * 1024 * 1024,  // 默认 10MB
                     size_t maxFiles = 10)                   // 最大轮转文件数
    {
        if (getInstance() != nullptr)
            return; // 已初始化

        try
        {
            if (!std::filesystem::exists(logDir))
            {
                std::filesystem::create_directories(logDir);
            }

            // 控制台日志
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

            // 文件日志（日期 + 滚动）
            auto file_sink = std::make_shared<daily_rotating_file_sink_mt>(
                logDir + "/logfile", maxFileSize, maxFiles);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

            getInstance() = std::make_shared<spdlog::logger>(
                "global_logger", spdlog::sinks_init_list{console_sink, file_sink});

            switch(level)
            {
                case Level::Debug: getInstance()->set_level(spdlog::level::debug); break;
                case Level::Info:  getInstance()->set_level(spdlog::level::info); break;
                case Level::Warn:  getInstance()->set_level(spdlog::level::warn); break;
                case Level::Error: getInstance()->set_level(spdlog::level::err); break;
            }

            getInstance()->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            printf("Logger initialization failed: %s\n", ex.what());
        }
    }

private:
    Logger() = default;
};
