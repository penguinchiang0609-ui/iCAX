#pragma once
#include "Services.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 服务接口
        */
        class _SERVICE_EXP IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IService() = default;

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() = 0;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() = 0;
        };
    }
}