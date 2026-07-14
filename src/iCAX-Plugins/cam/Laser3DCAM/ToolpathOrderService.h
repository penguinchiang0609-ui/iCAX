#pragma once

#include "Laser3DCAMExport.h"

#include "Data/Variant.h"
#include "Data/uuid.h"
#include "Services/IService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class ISceneContext;
    }

    namespace CAM
    {
        /*
        * @brief Program child 引用。
        * @details
        *   排序的权威结果是 Block.Children[]，因此排序结果也只表达 child 引用顺序。
        */
        struct _LASER_3D_CAM_EXP SProgramChildRef final
        {
            std::string Kind;                 //!< block 或 path。
            iCAX::Data::uuid EntityID;         //!< 子 Block 或 Path 的 EntityID。
        };

        /*
        * @brief 刀路排序请求。
        */
        struct _LASER_3D_CAM_EXP SToolpathOrderRequest final
        {
            iCAX::Data::uuid BlockEntityID;    //!< 要排序的 Block；nil 表示程序根 Block。
            std::string Strategy = "NearestNeighbor"; //!< 排序策略。
            iCAX::Data::ObjectMap Options;     //!< 权重、锁定项、下刀点策略等。
            bool bIncludeNestedBlocks = false; //!< 是否展开子 Block 参与排序。
        };

        /*
        * @brief 刀路排序计划。
        */
        struct _LASER_3D_CAM_EXP SToolpathOrderPlan final
        {
            iCAX::Data::uuid BlockEntityID;    //!< 计划应用到的 Block。
            std::string Strategy;              //!< 实际使用的策略。
            std::vector<SProgramChildRef> OrderedChildren; //!< 目标 Children 顺序。
            double dEstimatedCost = 0.0;        //!< 预估代价。
            iCAX::Data::ObjectMap CostBreakdown; //!< 代价明细。
            iCAX::Data::VariantArray Diagnostics; //!< 诊断信息。
        };

        /*
        * @brief 刀路排序服务。
        * @details
        *   服务负责生成排序计划和应用排序计划。CommandTargets 负责解析命令、开启 undo step 和返回结果。
        */
        class _LASER_3D_CAM_EXP IToolpathOrderService : public iCAX::Services::IService
        {
        public:
            ~IToolpathOrderService() override = default;

        public:
            virtual SToolpathOrderPlan BuildOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderRequest& Request_) = 0;

            virtual bool ApplyOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderPlan& Plan_,
                OUT std::string& strError_) = 0;
        };
    }
}
