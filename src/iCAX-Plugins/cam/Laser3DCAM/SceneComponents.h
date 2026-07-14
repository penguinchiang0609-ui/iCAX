#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 三维线条切割 CAM 场景启动组件。
        * @details
        *   该组件由产品 manifest 作为 startupComponent 挂到 MetaEntity 上，只负责触发
        *   SceneBootstrapBehaviour。它不保存业务数据；真实项目入口仍然是 CRootComponent。
        */
        class CSceneBootstrapComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CSceneBootstrapComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CSceneBootstrapComponent)
        };

        /*
        * @brief 三维线条切割 CAM 项目根组件。
        * @details
        *   该组件挂在 Repository 的 MetaEntity 上，作为产品业务入口。
        *   它只保存当前主对象引用，不保存业务集合，也不保存几何大对象。
        *   具体工件、机床、刀路、切割图层、显示图层、顺序规划等仍然是普通 Entity。
        */
        class CRootComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CRootComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CRootComponent)

            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, ActiveWorkpieceID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, ActiveToolAssemblyID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, DefaultCuttingLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, DefaultVisibleLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, ActiveOrderPlanID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, LatestSafetyCheckID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, ActiveSimulationID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CRootComponent, iCAX::Data::uuid, ProgramRootBlockID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        };
    }
}
