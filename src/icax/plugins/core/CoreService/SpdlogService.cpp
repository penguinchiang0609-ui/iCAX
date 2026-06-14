#include "pch.h"
#include "SpdlogService.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <filesystem>
#include "Data/PropertyBag.h"
#include "Services/ServicesHelper.h"
#include "ISettingService.h"

namespace fs = std::filesystem;

//!< 构造函数
iCAX::Core::CSpdlogService::CSpdlogService(IN const std::shared_ptr<ISettingService>& pSetting_)
    : m_pSettingService(pSetting_)
{
}

//!< 析构函数
iCAX::Core::CSpdlogService::~CSpdlogService()
{
    if (m_pLogger)
    {
        m_pLogger->flush();
        spdlog::drop(m_pLogger->name());
        m_pLogger.reset();
    }
}

//! 加载
void iCAX::Core::CSpdlogService::OnLoad()
{
    iCAX::Core::LogLevel _nLogLevel = (iCAX::Core::LogLevel)m_pSettingService->GetApplicationSetting().Get("Logger", "LogLeverl", iCAX::Data::Variant(0)).To<int>();
    std::string _strLogFileName = m_pSettingService->GetApplicationSetting().Get("Logger", "LogFile", std::string("Logs/iCAX_Log.log")).To<std::string>();
    int _nMaxFiles = m_pSettingService->GetApplicationSetting().Get("Logger", "MaxFiles", iCAX::Data::Variant(20)).To<int>();

    m_pLogger = spdlog::daily_logger_mt("file_logger", _strLogFileName, 0, 0, false);//!< 00:00:00 新生成
    //! [% Y - % m - % d % H:% M : % S. % e] [% ^%l % $] [% s:% #  % !] [% ^%t % $] % v
    //!   %Y-%m-%d %H:%M:%S.%e → 时间（精确到毫秒）
    //!   %l → 日志等级
    //!   %v → 日志内容（也就是你传给 Info/Debug 的 message）
    //!   %s → 文件名
    //!   %# → 行号
    //!   %! → 函数名
    //!   %t → 线程 id
    //!   %n → logger 名称
    //!   示例：[2025-09-15 22:59:00.123] [info] [main.cpp:42 main][1034] Hello world!
    m_pLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%# %!] %v");
    m_pLogger->flush_on(spdlog::level::info);

    switch (_nLogLevel)
    {
    case kTrace: m_pLogger->set_level(spdlog::level::trace); break;
    case kDebug: m_pLogger->set_level(spdlog::level::debug); break;
    case kInfo:  m_pLogger->set_level(spdlog::level::info); break;
    case kWarn:  m_pLogger->set_level(spdlog::level::warn); break;
    case kError: m_pLogger->set_level(spdlog::level::err); break;
    case kFault: m_pLogger->set_level(spdlog::level::critical); break;
    default: m_pLogger->set_level(spdlog::level::info); break;
    }

    //!< 手动清理旧日志，最多保留 nMaxFiles_ 个
    //!< 高级别的spdlog已内置支持最大数量，如果后续升级了，直接将参数传递给spdlog，下面的代码注释掉即可
    try
    {
        fs::path filePath(_strLogFileName);
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

        if (logs.size() > _nMaxFiles)
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

//! 卸载
void iCAX::Core::CSpdlogService::OnUnload()
{
    if (m_pLogger)
    {
        m_pLogger->flush();
        spdlog::drop(m_pLogger->name());
        m_pLogger.reset();
        m_pLogger = nullptr;
    }
    m_pSettingService = nullptr;
}

//!< 记录
void iCAX::Core::CSpdlogService::Trace(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->trace("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::Core::CSpdlogService::Debug(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->debug("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::Core::CSpdlogService::Info(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->info("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::Core::CSpdlogService::Warn(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->warn("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::Core::CSpdlogService::Error(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->error("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//!< 记录
void iCAX::Core::CSpdlogService::Fault(IN const std::string& strMessage_, IN const std::source_location& Loc_)
{
    if (m_pLogger)
        m_pLogger->critical("[{}:{} {}] {}", Loc_.file_name(), Loc_.line(), Loc_.function_name(), strMessage_);
}

//// 自动注册 TaskService
//static iCAX::Core::AutoRegisterService<iCAX::Core::ILogService, iCAX::Core::CSpdlogService, iCAX::Core::ISettingService> s_autoRegister;
