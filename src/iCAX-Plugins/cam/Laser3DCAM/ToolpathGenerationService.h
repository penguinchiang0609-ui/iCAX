#pragma once

#include "FeatureRecognitionService.h"
#include "Laser3DCAMExport.h"

#include "Data/Variant.h"
#include "Data/uuid.h"
#include "Services/IService.h"

#include <string>

namespace iCAX
{
    namespace Project
    {
        class IProjectContext;
    }

    namespace CAM
    {
        /*
        * @brief 刀路生成请求。
        * @details
        *   输入是参数化特征，不是 BRep，也不是前端选择对象。BRep 到特征的转换由 FeatureRecognitionService 负责。
        */
        struct _LASER_3D_CAM_EXP SToolpathGenerationRequest final
        {
            iCAX::Data::uuid WorkpieceEntityID;      //!< 目标工件 EntityID。
            SRecognizedFeature Feature;              //!< 已识别或手动构造的参数化特征。
            iCAX::Data::uuid CuttingLayerID;         //!< 目标 CuttingLayer；nil 表示使用默认层。
            iCAX::Data::uuid VisibleLayerID;         //!< 目标 VisibleLayer；nil 表示使用默认层。
            iCAX::Data::ObjectMap Options;           //!< 生成选项，如误差、方向、引入引出策略。
        };

        /*
        * @brief 生成出的刀路数据。
        * @details
        *   该结构只表达可落库数据，不直接修改 Repository。
        */
        struct _LASER_3D_CAM_EXP SGeneratedToolpath final
        {
            std::string Name;                        //!< 建议刀路名称。
            std::string PathKind = "Cut";            //!< Cut/AirMove/Mark 等。
            std::string Operation;                   //!< CutLoop/CutHole/CutBevelHole 等。
            std::string Role;                        //!< cut/hole/bevel 等。
            iCAX::Data::ObjectMap SourceFeature;     //!< 参数化特征快照。
            iCAX::Data::VariantArray CurveSegments;  //!< 世界坐标曲线段。
            iCAX::Data::VariantArray PoseSeeds;      //!< 可选姿态种子，不等于完整 PoseField。
            iCAX::Data::VariantArray Diagnostics;    //!< 生成诊断。
        };

        /*
        * @brief 刀路生成服务。
        * @details
        *   该服务负责把参数化特征转换成刀路曲线与初始工艺数据，不负责排序、不负责写 Repository。
        */
        class _LASER_3D_CAM_EXP IToolpathGenerationService : public iCAX::Services::IService
        {
        public:
            ~IToolpathGenerationService() override = default;

        public:
            virtual SGeneratedToolpath Generate(
                IN iCAX::Project::IProjectContext& Project_,
                IN const SToolpathGenerationRequest& Request_) = 0;
        };
    }
}
