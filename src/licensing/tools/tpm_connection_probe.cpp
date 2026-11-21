// Connection diagnosis only: no TPM commands, keys, enrollment, network,
// license files, service configuration or TPM provisioning operations.
#include "TpmStatus.h"
#include <iostream>

int main() {
    using namespace tube::license::tpm;
    std::cout << "diagnostic=TubeDesigner TPM connection v1\n";
    HANDLE token{};
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation{}; DWORD size{};
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            std::cout << "elevated=" << (elevation.TokenIsElevated ? "true" : "false") << '\n';
        CloseHandle(token);
    }
    TPM_DEVICE_INFO info{};
    const auto deviceStatus = Tbsi_GetDeviceInfo(sizeof(info), &info);
    std::cout << "deviceInfoStatus=" << StatusHex(deviceStatus) << '\n';
    if (deviceStatus == TBS_SUCCESS) {
        std::cout << "tpmVersion=" << info.tpmVersion << '\n';
        if (info.tpmVersion != TPM_VERSION_20)
            std::cout << "deviceMessage=当前设备不是 TPM 2.0，无法用于此授权流程。\n";
    } else {
        std::cout << "deviceMessage=" << StatusGuidance(deviceStatus) << '\n';
    }
    TBS_CONTEXT_PARAMS2 params{};
    params.version = TBS_CONTEXT_VERSION_TWO;
    params.includeTpm20 = 1;
    TBS_HCONTEXT context{};
    const auto contextStatus = Tbsi_Context_Create(reinterpret_cast<TBS_CONTEXT_PARAMS*>(&params), &context);
    std::cout << "contextStatus=" << StatusHex(contextStatus) << '\n';
    if (contextStatus == TBS_SUCCESS) {
        const auto closeStatus = Tbsip_Context_Close(context);
        std::cout << "closeStatus=" << StatusHex(closeStatus) << '\n';
        std::cout << "message=本次 TPM 2.0 连接成功；此结果仅验证连接，不代表授权申请或激活已完成。\n";
    } else {
        std::cout << "message=" << TransportError("无法连接 TPM 2.0", contextStatus).what() << '\n';
    }
    return contextStatus == TBS_SUCCESS ? 0 : 1;
}
