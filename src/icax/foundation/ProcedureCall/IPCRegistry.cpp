#include "pch.h"
#include "IPCRegistry.h"
#include <memory>
#include "PCRegistry.h"
#include <string>

//! 获取过程调用注册表
std::shared_ptr<iCAX::PC::IPCRegistry> iCAX::PC::GetGlobalPCRegistry()
{
    static std::shared_ptr<IPCRegistry> _PCRegistry = std::make_shared<CPCRegistry>();
    return _PCRegistry;
}
