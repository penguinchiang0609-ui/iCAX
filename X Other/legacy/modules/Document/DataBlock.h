#pragma once
#include "Data/Variant.h"
#include "Data/uuid.h"
#include <cstdint>

namespace iCAX
{
    namespace Document
    {
        /*
        * @brief 域数据
        */
        struct Domain
        {
        public:
            iCAX::Data::uuid RepositoryID;                  //! 仓储ID
            iCAX::Data::uuid DomainID;                      //! 域名ID
        };
        /*
        * @brief 组件规约
        */
        struct Component
        {
        public:
            uint32_t nDomainIndex;                          //! 域索引
            uint32_t nSchemaIndex;                          //! 规约索引
            iCAX::Data::uuid EntityID;                      //! 实体ID
            iCAX::Data::PropertySet Properties;             //! 字段字典
        };

        /*
        * @brief 组件块
        * @details
        *   此处同一种组件只存在一个版本
        *   这么做的直接结果是高版本打开低版本时，打开即升级到高版本的数据。保存后，低版本软件将无法打开。（unity3d\unrealengine均如此）
        *   如此选择的原因是如果允许多个版本同时存在，System层即Behaviour逻辑代码会爆炸，难以维护
        */
        struct DataBlock
        {
        public:
            uint32_t nDomainCount;
            Domain* pDomain;
            uint32_t nComponentCount;
            Component* pComponent;
        };
    }
}