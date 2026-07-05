#pragma once

#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "ProjectContextExport.h"

#include <string>

namespace iCAX
{
    namespace Project
    {
        /*
        * @brief 项目级运行上下文。
        * @details
        *   ProjectContext 表达项目级管理容器。Project 只承载跟图纸走的项目身份、路径和 ProjectSetting。
        *   Repository、Universe、ResourceLibrary、PDOHub、MailChannel 和工作线程都归属 SceneContext。
        */
        class _PROJECT_CONTEXT_EXP IProjectContext
        {
        public:
            IProjectContext();
            virtual ~IProjectContext();

            IProjectContext(const IProjectContext&) = delete;
            IProjectContext& operator=(const IProjectContext&) = delete;

        public:
            /*
            * @brief 获取项目 ID。
            */
            virtual const iCAX::Data::uuid& GetProjectID() const = 0;

            /*
            * @brief 获取项目名称。
            */
            virtual const std::string& GetProjectName() const = 0;

            /*
            * @brief 获取项目路径。
            */
            virtual const std::string& GetProjectPath() const = 0;

            /*
            * @brief 获取项目级参数。
            * @details 项目级设置跟项目文件走，不承载产品级设置或应用级设置。
            */
            virtual iCAX::Data::PropertyBag& Settings() = 0;
            virtual const iCAX::Data::PropertyBag& Settings() const = 0;
        };
    }
}
