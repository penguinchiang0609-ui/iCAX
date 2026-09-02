#pragma once

#include "GeometryData.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::GeometryData::Tube
{
    /*
    * Tube 中性几何只表达二维材料区域、三维实体集合和表面内容。
    * 圆管、方管、多腔管、工字钢等名称不得成为几何类型或求值分支。
    */

    enum class EPlanarFillRule : std::uint8_t
    {
        NonZero = 0,
        EvenOdd = 1
    };

    struct SRegionCurve2 final
    {
        std::string ID;
        iCAX::GeometryData::CurveSegment2 Segment;
    };

    struct SRegionLoop2 final
    {
        std::string ID;
        std::vector<SRegionCurve2> Curves;
    };

    /*
    * @brief 有向二维材料区域。
    * @details 每个边界 Loop 均约定材料位于曲线行进方向左侧。
    *          因此外边界通常逆时针，内腔边界通常顺时针；算法不按管型分派。
    */
    struct CPlanarRegion2 final
    {
        EPlanarFillRule FillRule = EPlanarFillRule::NonZero;
        std::vector<SRegionLoop2> Boundaries;
    };

    /*
    * @brief 稳定侧壁引用。
    * @details ExtrusionNodeID + LoopID 唯一确定整个闭环的 SideAtlas；
    *          CurveID 为空时引用整个 SideAtlas，否则引用单条曲线拉伸出的母曲面。
    */
    struct SBoundarySurfaceRef final
    {
        std::string ExtrusionNodeID;
        std::string LoopID;
        std::string CurveID;
    };

    /*
    * BRep 只能证明当前几何，不能总是唯一证明原始建模参数。参数的可观测性必须
    * 与参数值一起保存，避免把被其他特征遮挡的长度或位置伪装成确定设计意图。
    */
    enum class EParameterObservability : std::uint8_t
    {
        Observed = 0,
        Reconstructed = 1,
        Underconstrained = 2
    };

    struct SScalarParameter final
    {
        double Value = 0.0;
        EParameterObservability Observability = EParameterObservability::Observed;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
        std::map<std::string, std::string> Metadata;
    };

    enum class EMaterialTransitionKind : std::uint8_t
    {
        Unknown = 0,
        EnterMaterial = 1,
        LeaveMaterial = 2
    };

    /*
    * @brief 特征沿自身构造方向与母材边界的一次稳定穿越引用。
    * @details TravelOrdinal 和 WitnessStation 是导入时的重匹配证据，不是 OCC Face ID；
    *          Support 可在已确定 Side Atlas 时进一步锁定具体截面边界。
    */
    struct SMaterialBoundaryReference final
    {
        std::string ID;
        std::string HostNodeID;
        std::uint32_t TravelOrdinal = 0;
        EMaterialTransitionKind Transition = EMaterialTransitionKind::Unknown;
        SScalarParameter WitnessStation;
        std::optional<SBoundarySurfaceRef> Support;
        std::map<std::string, std::string> Metadata;
    };

    enum class ESurfaceEvidenceRole : std::uint8_t
    {
        Unknown = 0,
        InheritedHost = 1,
        GeneratedLateral = 2,
        GeneratedTermination = 3,
        GeneratedModifier = 4
    };

    /*
    * @brief 最终零件 P 上为某个可编辑特征解释提供证据的曲面面片。
    * @details SourceShapeID 只在 SourceResourceID 指定的 BRep 资源内有效。同一个面片
    *          可以同时进入多个 optional 候选，不能把本结构当成互斥 Face 分区。
    */
    struct SSurfaceEvidenceReference final
    {
        std::string SourceResourceID;
        std::uint64_t SourceShapeID = 0;
        ESurfaceEvidenceRole Role = ESurfaceEvidenceRole::Unknown;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    enum class EMaterialSpanEndpointKind : std::uint8_t
    {
        Unknown = 0,
        HostBoundary = 1,
        FeatureTermination = 2
    };

    struct SMaterialSpanEndpoint final
    {
        EMaterialSpanEndpointKind Kind = EMaterialSpanEndpointKind::Unknown;
        SScalarParameter Station;
        std::optional<SMaterialBoundaryReference> Boundary;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 一个生成体在母材中实际穿过的一段连续材料。
    * @details 同一个支管生成体可在多腔管上产生多个互不连通的 MaterialSpan；这些跨度
    *          属于同一特征，而不是因为 B-P 不连通就被强制拆成多个孔。
    */
    struct SMaterialSpan final
    {
        std::string ID;
        SMaterialSpanEndpoint Start;
        SMaterialSpanEndpoint End;
        std::vector<std::uint32_t> RemovalComponentOrdinals;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 任意三维减材特征在母材上产生的稳定开口引用。
    * @details OpeningID 属于 SourceFeatureNodeID 的求值结果；Boundary 保存当前快照
    *          与母材相交的重匹配证据。坡口和壁厚剖面不能只引用拉伸体 SideAtlas，
    *          因为开口也可能由锥体、半空间或复合候选产生。
    */
    struct SFeatureOpeningRef final
    {
        std::string SourceFeatureNodeID;
        std::string OpeningID;
        std::optional<SMaterialBoundaryReference> Boundary;
        std::map<std::string, std::string> Metadata;
    };

    enum class EFeatureExtentRule : std::uint8_t
    {
        FixedInterval = 0,
        ThroughAllHostMaterial = 1,
        ThroughSelectedHostBoundaries = 2,
        BlindFromHostBoundary = 3,
        Enclosed = 4
    };

    enum class EExtentDirection : std::uint8_t
    {
        Negative = 0,
        Positive = 1,
        Both = 2
    };

    /*
    * @brief 可随上游母材变化重新求值的特征范围。
    * @details PrimitiveFirst/PrimitiveLast 记录当前快照的有限求值范围；当
    *          RecomputeAgainstHost 为 true 时，ExtentRule 才是编辑后的权威范围定义。
    */
    struct SFeatureExtentConstraint final
    {
        EFeatureExtentRule Rule = EFeatureExtentRule::FixedInterval;
        EExtentDirection Direction = EExtentDirection::Both;
        std::string HostNodeID;
        SScalarParameter PrimitiveFirst;
        SScalarParameter PrimitiveLast;
        std::vector<SMaterialBoundaryReference> Boundaries;
        bool RecomputeAgainstHost = false;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    enum class EHostPlacementRule : std::uint8_t
    {
        FixedWorld = 0,
        RelativeToHostFrame = 1,
        AttachedToHostBoundary = 2
    };

    struct SHostPlacementConstraint final
    {
        EHostPlacementRule Rule = EHostPlacementRule::FixedWorld;
        std::string HostNodeID;
        std::optional<SBoundarySurfaceRef> Support;
        iCAX::GeometryData::Placement3 LocalFrame;
        SScalarParameter SupportU;
        SScalarParameter SupportV;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    enum class EFeatureMaterialRole : std::uint8_t
    {
        UnclassifiedRemoval = 0,
        Penetration = 1,
        Truncation = 2,
        BoundaryProfileModifier = 3
    };

    /*
    * @brief 几何生成体相对母材承担的角色，不是新的几何原语。
    * @details 例如“截断”可由半空间、拉伸体、锥体或多个局部生成体实现；Role 只说明
    *          它在宿主材料表达式中的关系，避免把截断重新退化成特殊曲线或高度场。
    */
    struct SFeatureMaterialEffect final
    {
        EFeatureMaterialRole Role = EFeatureMaterialRole::UnclassifiedRemoval;
        std::string HostNodeID;
        std::vector<std::string> SourceNodeIDs;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    struct SFeatureRelations final
    {
        std::vector<std::string> DependsOnNodeIDs;
        std::optional<SFeatureExtentConstraint> Extent;
        std::optional<SHostPlacementConstraint> Placement;
        std::optional<SFeatureMaterialEffect> MaterialEffect;
        std::vector<SSurfaceEvidenceReference> SurfaceEvidence;
        std::vector<SMaterialSpan> MaterialSpans;
        std::map<std::string, std::string> Metadata;
    };

    enum class ESideAtlasSeamRule : std::uint8_t
    {
        /* 截面边界上 X 最小、并以 Y 最小打破并列的位置。 */
        MinimumXThenY = 0
    };

    /*
    * @brief 一个截面闭环拉伸形成的稳定参数域。
    * @details u 是从 SeamRule 确定的缝线开始、沿“材料在左”方向增长的物理弧长，
    *          周期为 UPeriod；v 是 Frame.ZDirection 上的物理距离，范围为 [VFirst,VLast]。
    *          因此参数域不依赖 OCC Face 的天然 UV、拓扑枚举顺序或外壁/内壁配对。
    */
    struct SSideAtlasDefinition final
    {
        std::string ID;
        std::string LoopID;
        ESideAtlasSeamRule SeamRule = ESideAtlasSeamRule::MinimumXThenY;
        double UPeriod = 0.0;
        double VFirst = 0.0;
        double VLast = 0.0;
    };

    enum class ESectionPrimitiveRole : std::uint8_t
    {
        OuterBoundary = 0,
        Cavity = 1
    };

    enum class ESectionPrimitiveKind : std::uint8_t
    {
        CurveLoop = 0,
        Circle = 1,
        Rectangle = 2
    };

    /*
    * @brief 主体截面中可独立选择、编辑和重求值的 CAD 原语。
    * @details Geometry 仍以 Section.Boundaries 中 LoopID 指向的精确曲线环为权威表达；
    *   本结构保存从该环观察到的参数化解释。外轮廓与每个腔体都各有一个原语，
    *   不把“多腔管”硬编码成专用管型。
    */
    struct SSectionPrimitive final
    {
        std::string ID;
        std::string Label;
        std::string LoopID;
        ESectionPrimitiveRole Role = ESectionPrimitiveRole::OuterBoundary;
        ESectionPrimitiveKind Kind = ESectionPrimitiveKind::CurveLoop;
        iCAX::GeometryData::Point2 Center;
        double RotationRadians = 0.0;
        std::optional<SScalarParameter> Width;
        std::optional<SScalarParameter> Height;
        std::optional<SScalarParameter> Radius;
        EParameterObservability Observability = EParameterObservability::Observed;
        std::map<std::string, std::string> Metadata;
    };

    struct SExtrudedRegionNode final
    {
        CPlanarRegion2 Section;
        std::vector<SSectionPrimitive> SectionPrimitives;
        iCAX::GeometryData::Placement3 Frame;
        double First = 0.0;
        double Last = 0.0;
        std::vector<SSideAtlasDefinition> SideAtlases;
    };

    /*
    * @brief 截面沿轴向做线性一致缩放的实体。
    * @details Section 定义 Reference 位置处的截面；任意位置 w 的截面以 ScaleCenter
    *          为中心按 FirstScale/LastScale 的线性插值一致缩放。圆形截面得到圆锥或锥台，
    *          任意截面得到相似截面渐缩体，不引入圆孔、方孔等管型分支。
    */
    struct STaperedRegionNode final
    {
        CPlanarRegion2 Section;
        std::vector<SSectionPrimitive> SectionPrimitives;
        iCAX::GeometryData::Placement3 Frame;
        double First = 0.0;
        double Last = 0.0;
        double Reference = 0.0;
        iCAX::GeometryData::Point2 ScaleCenter;
        double FirstScale = 1.0;
        double LastScale = 1.0;
    };

    struct SHalfSpaceNode final
    {
        iCAX::GeometryData::Plane3 Boundary;
        bool KeepNegativeSide = true;
    };

    enum class EBooleanOperation : std::uint8_t
    {
        Union = 0,
        Difference = 1,
        Intersection = 2
    };

    struct SBooleanNode final
    {
        EBooleanOperation Operation = EBooleanOperation::Union;
        std::vector<std::string> Children;
    };

    struct STransformNode final
    {
        std::string Child;
        iCAX::GeometryData::Transform3 Transform;
    };

    /*
    * @brief 定义在稳定 Side Atlas 矩形域上的均匀标量网格。
    * @details Values 按 v-major、u-minor 顺序保存，格点包含四条边界，格间使用双线性插值。
    *          显式保存物理 u/v 范围，避免从 UVRegion 包围盒猜测周期跨缝后的参数域。
    */
    struct SScalarGrid2 final
    {
        double UFirst = 0.0;
        double ULast = 0.0;
        double VFirst = 0.0;
        double VLast = 0.0;
        std::uint32_t UCount = 0;
        std::uint32_t VCount = 0;
        std::vector<double> Values;
    };

    using ScalarField2 = std::variant<double, SScalarGrid2>;

    enum class EOpeningProfileInterpolation : std::uint8_t
    {
        PiecewiseConstant = 0,
        Linear = 1,
        PeriodicBSpline = 2
    };

    enum class EOpeningProfileFrameRule : std::uint8_t
    {
        /* 剖面二维平面法向为开口切向；二维基由源切割面和开口切向确定。 */
        SourceCutNormalPlane = 0,
        /* 无法稳定恢复名义切割面时，保存导入时观察到的局部参考架。 */
        ObservedLocalFrame = 1
    };

    /*
    * @brief 开口轮廓某一参数位置处、穿过材料厚度的二维剖面曲线。
    * @details Profile 位于该开口的局部法截面。直线只是普通坡口的特例；折线、圆弧
    *          和 NURBS 可以表达 X/K/J/U 形及任意变坡。ContourParameter 使用开口
    *          轮廓的物理弧长，不保存易变的 BRep Edge 参数。Profile 二维坐标约定为：
    *          X 沿源切割体的构造方向，Y 为相对源切割边界向减材侧的有符号偏移。
    */
    struct SOpeningProfileKey final
    {
        SScalarParameter ContourParameter;
        iCAX::GeometryData::CompositeCurve2 Profile;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    struct SOpeningProfileField final
    {
        double ContourFirst = 0.0;
        double ContourLast = 0.0;
        bool Periodic = false;
        EOpeningProfileInterpolation Interpolation =
            EOpeningProfileInterpolation::Linear;
        std::vector<SOpeningProfileKey> Keys;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief UV 区域沿支撑侧壁材料外法向提升得到的确定三维体。
    * @details 设 Side Atlas 为 S(u,v)，材料外法向为 N(u,v)，则本节点表示
    *          { S(u,v) + t*N(u,v) | (u,v) 属于 UVRegion，
    *            LowerNormalOffset(u,v) <= t <= UpperNormalOffset(u,v) }。
    *          N 由 Side Atlas 唯一导出，不保存方向场；固定方向贯穿体应使用 ExtrudedRegionNode。
    *          两个 Offset 是显式有符号距离场，不使用含糊的 ThroughMaterial 或外壁/内壁配对语义。
    *          当本节点承担端部截断时，Relations.MaterialEffect.Role 为 Truncation；UVRegion
    *          描述被移除的一侧而不只是最终边界线，因而可以与盲孔、贯穿体继续做普通 CSG。
    */
    struct SWrappedVolumeNode final
    {
        SBoundarySurfaceRef Support;
        CPlanarRegion2 UVRegion;
        ScalarField2 LowerNormalOffset = 0.0;
        ScalarField2 UpperNormalOffset = 0.0;
    };

    /*
    * @brief 依附于已识别开口、沿轮廓变化的壁厚剖面扫掠修改体。
    * @details ProfileField 是权威几何表达。坡口角、钝边和深度都是剖面曲线相对
    *          名义切割面的派生量，不能作为中性几何的权威参数。EvaluatedVolumeNodeID
    *          指向与该剖面场严格等价的封闭减材体，供 CSG 求值和导入校验使用。
    */
    struct SOpeningProfileSweepNode final
    {
        std::string SourceCutNodeID;
        std::optional<SFeatureOpeningRef> TargetOpening;
        EOpeningProfileFrameRule FrameRule =
            EOpeningProfileFrameRule::SourceCutNormalPlane;
        SOpeningProfileField ProfileField;
        std::string EvaluatedVolumeNodeID;
        EParameterObservability Observability =
            EParameterObservability::Observed;
        std::map<std::string, std::string> Metadata;
    };

    enum class EAnalyticSurfaceKind : std::uint8_t
    {
        Unknown = 0,
        Plane = 1,
        Cylinder = 2,
        Cone = 3,
        Sphere = 4,
        Torus = 5,
        Extrusion = 6,
        Revolution = 7,
        BSpline = 8
    };

    struct SCompositeSurfacePatch final
    {
        std::uint64_t SourceShapeID = 0;
        EAnalyticSurfaceKind Kind = EAnalyticSurfaceKind::Unknown;
        std::vector<std::uint64_t> AdjacentShapeIDs;
        std::map<std::string, double> Parameters;
        std::map<std::string, std::string> Metadata;
    };

    enum class ECompositeEditCapability : std::uint8_t
    {
        WholeTransform = 0,
        BooleanReuse = 1,
        AnalyticFaceEdit = 2,
        ManualFeaturePromotion = 3
    };

    /*
    * @brief 自动参数化拆解失败后的局部、半参数化封闭体。
    * @details ExactBRep 是权威求值几何；AnalyticPatches 和邻接证据允许直接编辑及
    *          后续人工提升。转换器应只把真正无法拆解的局部放入本节点。
    */
    struct SCompositeVolumeNode final
    {
        std::string BRepResourceID;
        std::vector<std::uint64_t> RootShapeIDs;
        std::vector<SCompositeSurfacePatch> AnalyticPatches;
        std::vector<ECompositeEditCapability> EditCapabilities;
        bool Closed = true;
        bool Exact = true;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 无法参数化时的精确 BRep 保底节点。
    * @details 该节点保证转换不丢几何，但不会伪装成已识别特征。
    */
    struct SResidualBRepNode final
    {
        std::string BRepResourceID;
        std::vector<std::uint64_t> RootShapeIDs;
    };

    struct SInterpretationEvidence final
    {
        std::string Code;
        std::string Message;
        double Confidence = 0.0;
        std::vector<std::uint64_t> SourceShapeIDs;
    };

    /*
    * @brief 候选评价的可解释向量。
    * @details GeometryAccepted 是硬约束，不能由低复杂度抵消。RecommendationScore
    *          只在通过几何约束的候选之间排序，不是概率，也不等于人工选择。
    */
    struct SInterpretationEvaluation final
    {
        bool GeometryAccepted = false;
        double GeometryAcceptanceTolerance = 1.0e-4;
        double RelativeGeometryError = 1.0;
        double RelativeOvercut = 1.0;
        double RelativeUncovered = 1.0;
        double EvidenceCoverage = 0.0;
        double ParameterObservability = 0.0;
        double ModelComplexity = 1.0;
        double FieldComplexity = 1.0;
        double PerturbationStability = 0.0;
        double EditStability = 0.0;
        double RecommendationScore = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 同一局部几何的一个可选构造解释。
    * @details CandidateNodeID 指向完整、可独立求值的候选子图。AlternativeNode 的所有候选
    *          必须在容差内几何等价，但可具有不同可编辑结构，例如“真锥形贯穿”与
    *          “等截面贯穿 + 坡口附加减材”。
    */
    struct SInterpretationCandidate final
    {
        std::string ID;
        std::string Label;
        std::string CandidateNodeID;
        bool Optional = true;
        double Confidence = 0.0;
        /* 候选子图相对被解释局部几何的对称差体积比例。 */
        double RelativeGeometryError = 1.0;
        SInterpretationEvaluation Evaluation;
        std::vector<SInterpretationEvidence> Evidence;
        std::map<std::string, std::string> Metadata;
    };

    enum class EAlternativeSelectionSource : std::uint8_t
    {
        Unresolved = 0,
        Recommendation = 1,
        Strategy = 2,
        User = 3
    };

    /*
    * @brief 保留多个等价解释的 CSG 选择节点。
    * @details RecommendedCandidateID 只用于默认预览和后处理建议；SelectedCandidateID 为空表示
    *          尚未定案。人工或策略选择只改变解释，不应改变候选子图的求值几何。
    */
    struct SAlternativeNode final
    {
        std::vector<SInterpretationCandidate> Candidates;
        std::string RecommendedCandidateID;
        std::string SelectedCandidateID;
        EAlternativeSelectionSource SelectionSource = EAlternativeSelectionSource::Unresolved;
    };

    /*
    * @brief 一个减材构造相对原始材料边界的几何穿越关系。
    * @details 这里只记录几何事实，不决定激光切割、半切、打标、钻削或铣削等加工策略。
    */
    enum class ERemovalExtentKind : std::uint8_t
    {
        Unknown = 0,
        Through = 1,
        Blind = 2,
        Bubble = 3
    };

    struct SRemovalSemantics final
    {
        ERemovalExtentKind Extent = ERemovalExtentKind::Unknown;
        /* 减材体边界中与原始母材边界重合的开口面片数量。 */
        std::uint32_t BoundaryOpeningPatchCount = 0;
        /* 构造轴向范围中位于材料内部的终止端数量；锥尖也属于终止端。 */
        std::uint32_t InternalTerminationCount = 0;
        double Confidence = 0.0;
        std::map<std::string, std::string> Metadata;
    };

    using SolidNodeData = std::variant<
        SExtrudedRegionNode,
        STaperedRegionNode,
        SHalfSpaceNode,
        SBooleanNode,
        STransformNode,
        SWrappedVolumeNode,
        SOpeningProfileSweepNode,
        SCompositeVolumeNode,
        SResidualBRepNode,
        SAlternativeNode>;

    struct SSolidNode final
    {
        std::string ID;
        std::string Label;
        SolidNodeData Data;
        /* 构造依赖和编辑后的重算规则；不包含机床或加工策略。 */
        std::optional<SFeatureRelations> Relations;
        std::optional<SRemovalSemantics> RemovalSemantics;
        std::map<std::string, std::string> Metadata;
    };

    /*
    * @brief 零深度的通用 UV 矢量几何。
    * @details 只说明曲线或区域位于支撑面上，不指定打标、浅刻、铣削或切割工艺。
    */
    struct SUVVectorGeometry final
    {
        SBoundarySurfaceRef Support;
        std::vector<iCAX::GeometryData::CompositeCurve2> Paths;
        std::vector<CPlanarRegion2> Regions;
        iCAX::GeometryData::Transform2 UVTransform;
    };

    struct SUVImage final
    {
        SBoundarySurfaceRef Support;
        std::string ImageResourceID;
        iCAX::GeometryData::Transform2 UVTransform;
        double PhysicalWidth = 0.0;
        double PhysicalHeight = 0.0;
        std::string AlphaMode = "Straight";
        std::string SamplingMode = "Linear";
    };

    using UVFeatureData = std::variant<SUVVectorGeometry, SUVImage>;

    struct SUVFeature final
    {
        std::string ID;
        std::string Label;
        UVFeatureData Data;
        std::map<std::string, std::string> Metadata;
    };

    enum class ERecognitionStatus : std::uint8_t
    {
        Exact = 0,
        EquivalentButAmbiguous = 1,
        Partial = 2,
        Failed = 3
    };

    struct SRecognitionDiagnostic final
    {
        std::string Code;
        std::string Message;
        double Confidence = 0.0;
        std::vector<std::uint64_t> SourceFaceIDs;
    };

    struct CTubeNeutralGeometry final
    {
        inline static constexpr const char* kResourceTypeName = "tube.neutral_geometry";

        std::uint64_t Version = 0;
        std::string BaseNodeID;
        std::string RootNodeID;
        std::vector<SSolidNode> SolidNodes;
        /* 零深度表面特征；不参与实体 CSG Boolean。 */
        std::vector<SUVFeature> UVFeatures;

        ERecognitionStatus RecognitionStatus = ERecognitionStatus::Failed;
        double Confidence = 0.0;
        /* 对完整 CSG（含 residual）求值后，与源 BRep 的对称差体积比例。 */
        double RelativeVolumeError = 1.0;
        /* 仍由 CompositeVolume 或 ResidualBRep 承担、尚未完全参数化的体积比例。 */
        double RelativeUnparameterizedVolume = 1.0;
        /* 完全不透明、连半参数化复合体都无法建立的体积比例。 */
        double RelativeOpaqueResidualVolume = 1.0;
        std::vector<SRecognitionDiagnostic> Diagnostics;
        std::map<std::string, std::string> Metadata;
    };
}
