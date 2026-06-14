#include "pch.h"
#include "SpdlogService.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <filesystem>

namespace fs = std::filesystem;

//!< 构造函数
iCAX::LogService::SpdlogService::SpdlogService()
{
}

//!< 析构函数
iCAX::LogService::SpdlogService::~SpdlogService()
{
    if (m_pLogger)
    {
        m_pLogger->flush();
        spdlog::drop(m_pLogger->name());
        m_pLogger.reset();
    }
}

//!< 初始化
void iCAX::LogService::SpdlogService::Initalize(IN const std::string& strFileName_, const int& nMaxFiles_)
{
    m_pLogger = spdlog::daily_logger_mt("file_logger", strFileName_, 0, 0, false);//!< 00:00:00 新生成
    m_pLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%# %!] %v");
    m_pLogger->flush_on(spdlog::level::info);

    //!< 手动清理旧日志，最多保留 nMaxFiles_ 个
    //!< 高级别的spdlog已内置支持最大数量，如果后续升级了，直接将参数传递给spdlog，下面的代码注释掉即可
    try
    {
        fs::path filePath(strFileName_);
        fs::path logDir = filePath.parent_path();
        std::string baseName = filePath.stem().string(); // 文件名前缀

        std::vector<fs::directory_entry> logs;
        for (auto& entry : fs::directory_iterator(logDir))
        {
            if (entry.is_regular_file())
            {
                auto fname = entry.path().filename().string();
                if (fname.find(baseName) != std::string::npos)
                    logs.push_back(entry);
            }
        }

        if (logs.size() > nMaxFiles_)
        {
            sort(logs.begin(), logs.end(),
                [](auto& a, auto& b) {
                    return a.last_write_time() < b.last_write_time();
                });

            for (size_t i = 0; i < logs.size() - 20; ++i)
            {
                fs::remove(logs[i].path());
            }
        }
    }
    catch (...)
    {
        // 避免因为文件系统异常导致程序崩溃
    }
}

//!< 设置日志级别
void iCAX::LogService::SpdlogService::SetLevel(IN const iCAX::LogService::LogLevel& nLevel_)
{
    if (!m_pLogger) return;

    switch (nLevel_)
    {
    case kTrace: m_pLogger->set_level(spdlog::level::trace); break;
    case kDebug: m_pLogger->set_level(spdlog::level::debug); break;
    case kInfo:  m_pLogger->set_level(spdlog::level::info); break;
    case kWarn:  m_pLogger->set_level(spdlog::level::warn); break;
    case kError: m_pLogger->set_level(spdlog::level::err); break;
    case kFault: m_pLogger->set_level(spdlog::level::critical); break;
    default: m_pLogger->set_level(spdlog::level::info); break;
    }
}

//!< 记录
void iCAX::LogService::SpdlogService::Trace(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->trace("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::LogService::SpdlogService::Debug(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->debug("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::LogService::SpdlogService::Info(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->info("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::LogService::SpdlogService::Warn(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->warn("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::LogService::SpdlogService::Error(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->error("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::LogService::SpdlogService::Fault(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->critical("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}
