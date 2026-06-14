#pragma once
#include "Sample.h"
#include "Services/IService.h"

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
        class _SAMPLESERVICE_EXP ISampleService : public iCAX::Services::IService
        {
        public:
            ISampleService() = default;
            virtual ~ISampleService() = default;

        public:
            virtual int Calc(IN const int& nA_, IN const int& nB_) = 0;
        };

    }
}