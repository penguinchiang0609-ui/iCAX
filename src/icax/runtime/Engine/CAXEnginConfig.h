#pragma once
#include "Engine.h"
#include <string>


namespace iCAX
{
    namespace Engine
    {
        /*
        * @brief 配置文件
        */
        struct _ENGINE_EXP CAXEnginConfig
        {
            /*
            * @brief 应用程序设置路径
            */
            std::string strAppSettingPath;

            ///*
            //* @brief 用户设置路径
            //*/
            //std::string strUsrSettingPath;

            ///*
            //* @brief 插件目录
            //*/
            //std::string strPluginDirectory;

            ///*
            //* @brief 第三方依赖目录
            //*/
            //std::string strThirdPartyDirectory;

            /*
            * @brief 插件配置文件路径
            */
            std::string strPluginConfigPath;

            /*
            * @brief 启动项配置信息
            */
            std::string strStartupPath;
        };
    }
}