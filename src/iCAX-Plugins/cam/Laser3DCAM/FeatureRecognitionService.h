#pragma once

#include "Laser3DCAMExport.h"

#include "Data/Variant.h"
#include "Services/IService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class IProjectContext;
    }

    namespace CAM
    {
        /*
        * @brief BRep 模型输入。
        * @details
        *   FeatureRecognitionService 的输入必须是 BRep 模型资源，而不是前端拾取缓存或显示 mesh。
        *   资源对象可以由 OCC、CGAL 或其他内核插件创建；本接口只依赖 ResourceID，避免产品核心绑定具体几何内核。
        */
        struct _LASER_3D_CAM_EXP SBRepModelInput final
        {
            std::string BRepResourceID;                 //!< BRep 模型资源 key。
            uint64_t nExpectedDataVersion = 0;          //!< 调用方期望的数据版本；0 表示不强制检查。
            iCAX::Data::ObjectMap Options;              //!< 内核相关读取选项。
        };

        /*
        * @brief 特征定义。
        * @details
        *   定义“要识别什么”。例如 Hole、BevelHole、OuterLoop、OpenEdge 等。
        *   Parameters 用于表达该类特征的参数约束，如半径范围、坡口角范围、最小深度等。
        */
        struct _LASER_3D_CAM_EXP SFeatureDefinition final
        {
            std::string Kind;                           //!< 特征类型，如 Hole/BevelHole/OuterLoop。
            iCAX::Data::ObjectMap Parameters;           //!< 特征定义参数。
        };

        /*
        * @brief 特征识别请求。
        */
        struct _LASER_3D_CAM_EXP SFeatureRecognitionRequest final
        {
            SBRepModelInput BRep;                       //!< 待识别 BRep 模型。
            std::vector<SFeatureDefinition> Definitions; //!< 要识别的特征定义。
            iCAX::Data::ObjectMap Scope;                //!< 识别范围，如 faceIds/edgeIds/boundingBox/selection。
            iCAX::Data::ObjectMap Options;              //!< 全局识别选项，如 tolerance。
            bool bReturnPreviewCurves = true;           //!< 是否返回可预览的世界坐标曲线。
        };

        /*
        * @brief 参数化特征。
        * @details
        *   识别结果不是刀路 Entity，也不是 Resource 反向引用。它是算法输出的纯参数数据。
        *   例如孔特征可以通过 Parameters 表达 path、depth、axis、radius、bevelType、bevelAngle 等。
        */
        struct _LASER_3D_CAM_EXP SRecognizedFeature final
        {
            uint64_t nCandidateID = 0;                  //!< 本次识别结果内的候选编号。
            std::string Kind;                           //!< 特征类型。
            std::string Role;                           //!< 建议加工角色，如 cut/hole/bevel。
            double dConfidence = 0.0;                   //!< 识别置信度，范围由算法定义。
            iCAX::Data::ObjectMap Parameters;           //!< 参数化表达。
            iCAX::Data::VariantArray PreviewCurves;     //!< 可选世界坐标曲线段，用于预览和后续刀路生成。
            iCAX::Data::VariantArray SourceRefs;        //!< 可选 BRep 来源引用，如 face/edge/loop id。
            iCAX::Data::VariantArray Warnings;          //!< 候选级警告。
        };

        /*
        * @brief 特征识别结果。
        */
        struct _LASER_3D_CAM_EXP SFeatureRecognitionResult final
        {
            uint64_t nDataVersion = 0;                  //!< 基于输入 BRep 版本得到的结果版本。
            std::vector<SRecognizedFeature> Features;   //!< 参数化特征列表。
            iCAX::Data::VariantArray Diagnostics;       //!< 服务级诊断信息。
        };

        /*
        * @brief 特征识别服务。
        * @details
        *   该服务只负责从 BRep 和特征定义识别出参数化特征，不创建 Path，不修改 Repository。
        */
        class _LASER_3D_CAM_EXP IFeatureRecognitionService : public iCAX::Services::IService
        {
        public:
            ~IFeatureRecognitionService() override = default;

        public:
            virtual SFeatureRecognitionResult Recognize(
                IN iCAX::Project::IProjectContext& Project_,
                IN const SFeatureRecognitionRequest& Request_) = 0;
        };
    }
}
