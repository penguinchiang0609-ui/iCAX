#pragma once
#include <windows.h>
#include <tbs.h>
#include <stdexcept>
#include <string>

namespace tube::license::tpm {
inline std::string StatusHex(TBS_RESULT status) {
    std::string text = "0x00000000";
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 8; ++i) text[9 - i] = digits[(status >> (i * 4)) & 15];
    return text;
}
inline const char* StatusGuidance(TBS_RESULT status) {
    switch (status) {
    case static_cast<TBS_RESULT>(TBS_E_TPM_NOT_FOUND):
        return "Windows 未找到兼容的 TPM。请在 tpm.msc 查看状态和规范版本，并确认 BIOS/UEFI 中已启用 TPM 2.0（Intel PTT / AMD fTPM）。";
    case static_cast<TBS_RESULT>(TBS_E_SERVICE_DISABLED):
        return "Windows 的 TPM Base Services（TBS）服务已被禁用，请由系统管理员检查服务配置。";
    case static_cast<TBS_RESULT>(TBS_E_SERVICE_NOT_RUNNING):
        return "Windows 的 TPM Base Services（TBS）服务未运行且无法启动，请由系统管理员检查该服务及系统事件记录。";
    case static_cast<TBS_RESULT>(TBS_E_SERVICE_START_PENDING):
        return "Windows 的 TPM Base Services（TBS）服务正在启动，请稍后重试。";
    case static_cast<TBS_RESULT>(TBS_E_ACCESS_DENIED):
        return "Windows 拒绝访问 TPM，请以管理员身份运行后重试；若仍失败，请检查系统访问策略。";
    case static_cast<TBS_RESULT>(TBS_E_PROVISIONING_NOT_ALLOWED):
        return "系统策略不允许 TPM 预配，请由系统管理员检查 TPM 管理策略。";
    case static_cast<TBS_RESULT>(TBS_E_PROVISIONING_INCOMPLETE):
        return "TPM 预配尚未完成，请在 Windows 安全中心检查安全处理器；仅在系统要求时重启后重试。";
    case static_cast<TBS_RESULT>(TBS_E_IOERROR):
        return "Windows 与 TPM 通信失败，请检查安全处理器状态及系统事件记录。";
    default:
        return "TPM 操作未完成，请将此错误码提供给技术支持，以确认具体原因。";
    }
}
class TransportError final : public std::runtime_error {
public:
    const TBS_RESULT status;
    TransportError(const char* operation, TBS_RESULT value)
        : std::runtime_error(std::string(operation) + "（Windows TBS " + StatusHex(value) + "）。" + StatusGuidance(value)), status(value) {}
};
inline void CheckStatus(TBS_RESULT status, const char* operation) {
    if (status != TBS_SUCCESS) throw TransportError(operation, status);
}
}
