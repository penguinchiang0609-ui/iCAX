#pragma once

#include "MachineResources.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 判断路径是否是当前 URDF Loader 负责的机床描述文件。
        * @param [in] strSourcePath_ 输入文件路径。
        * @return true 表示扩展名为 .urdf。
        */
        bool CanLoadURDFMachineDescription(
            IN const std::string& strSourcePath_);

        /*
        * @brief 加载 URDF 机床描述并转换为中性的 CMachineDescriptionResource。
        * @param [in] strSourcePath_ URDF 文件路径。
        * @param [out] Description_ 输出的机床中性结构。
        * @param [out] strError_ 失败时返回错误文本。
        * @return true 表示加载成功。
        */
        bool LoadURDFMachineDescription(
            IN const std::string& strSourcePath_,
            OUT CMachineDescriptionResource& Description_,
            OUT std::string& strError_);
    }
}
