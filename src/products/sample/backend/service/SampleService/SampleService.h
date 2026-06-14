#pragma once
#include "Sample.h"
#include "ISampleService.h"
#include "Services/ServicesHelper.h"

namespace iCAX
{
    namespace Sample
    {
        /**
        * @brief 层次组件
        * @details
        *   1、域中各个实体之间的父子关系
        *   2、域中所有实体都必须有该组件
        *   3、实体在创建后第一时间需要赋父子关系
        */
        class CSampleService final : public iCAX::Sample::ISampleService
        {
        public:
            CSampleService() = default;
            virtual ~CSampleService() = default;

        public://! IService成员
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override {}

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override {}

            virtual int Calc(IN const int& nA_, IN const int& nB_) override;

            AUTO_REGIST_SERVICE(ISampleService, CSampleService);
        };

    }
}