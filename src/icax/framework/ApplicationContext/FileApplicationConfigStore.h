#pragma once

#include "IApplicationConfigStore.h"

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 文件型应用配置存储。
        * @details 使用 VariantSerializer 序列化 PropertySet；配置根必须是对象。
        */
        class _APPLICATION_CONTEXT_EXP CFileApplicationConfigStore final : public IApplicationConfigStore
        {
        public:
            CFileApplicationConfigStore() = default;
            ~CFileApplicationConfigStore() override = default;

        public:
            /*
            * @brief 从文件加载应用设置。
            * @param [in] strPath_ 配置文件路径，不能为空。
            * @return 加载结果；文件不存在时返回空设置。
            */
            CApplicationSettings Load(IN const std::string& strPath_) const override;

            /*
            * @brief 保存应用设置到文件。
            * @param [in] strPath_ 配置文件路径，不能为空。
            * @param [in] Settings_ 待保存设置。
            * @details 保存前会创建父目录，并以截断方式写入。
            */
            void Save(IN const std::string& strPath_, IN const CApplicationSettings& Settings_) const override;
        };
    }
}
