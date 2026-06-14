#pragma once
#include "Sample.h"
#include "Data/uuid.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Database/IDomain.h"
#include "Database/IEntity.h"
#include "Data/CommonFunction.h"



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
        class _SAMPLECOMPONENT_EXP SampleComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(SampleComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(SampleComponent)

            //!< 测试字段
            DECLARED_ICAX_FIELD(SampleComponent, int, TestID, 0, [](const int& lhs_, const int& rhs_) {return lhs_ == rhs_; },
                [](const int& _lhs) {return _lhs; }, [](const iCAX::Data::PropertyValue& _lhs) {return _lhs.To<int>(); });
        };

    }
}