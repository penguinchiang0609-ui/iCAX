#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::TubeNesting
{
    /**
     * @brief 离散长度单位，由调用方统一量化后传入。
     * @details 推荐使用 0.01 mm 或设备能够稳定分辨的最小单位，避免浮点比较导致
     *          “界面显示可放入、求解器却判断超长”的边界不一致。
     */
    using Length = std::int64_t;

    /** A part has one longitudinal direction for both of its neighbours. */
    enum class Direction : std::uint8_t
    {
        Forward = 0,
        Reverse = 1,
    };

    /**
     * One certified trigonometric approximation of a periodic end boundary.
     * Coefficients and ResidualUpper use the same axial unit as Length.  The
     * represented function is
     *   Constant + sum(Cosine[k-1] cos(k theta) + Sine[k-1] sin(k theta)).
     */
    struct FourierLevel final
    {
        std::size_t Order = 0;
        double Constant = 0.0;
        std::vector<double> Cosine;
        std::vector<double> Sine;
        double ResidualUpper = 0.0;
        double FirstDerivativeUpper = 0.0;
        double SecondDerivativeUpper = 0.0;
        bool Certified = false;
    };

    enum class ProfileSourceQuality : std::uint8_t
    {
        Missing = 0,
        NativeCertified = 1,
        ApproximateCertified = 2,
        NumericOnly = 3,
    };

    struct PeriodicEndProfile final
    {
        std::string Schema = "icax.tube-end-spectrum.v1";
        std::string SourceHash;
        std::string CoordinateConvention = "theta-x-radians";
        ProfileSourceQuality SourceQuality = ProfileSourceQuality::Missing;
        bool CompleteSingleValued = false;
        std::vector<FourierLevel> Levels;
    };

    struct AngleInterval final
    {
        double MinimumRadians = 0.0;
        double MaximumRadians = 0.0;
    };

    struct AngleDomain final
    {
        /** Empty means the full periodic interval [0, 2*pi). */
        std::vector<AngleInterval> Intervals;
        std::vector<double> DiscreteRadians;
    };

    struct PairPartType final
    {
        std::string ID;
        std::string CompatibilityKey;
        std::string OrderKey;
        Length MaximumLength = 0;
        Length MaterialLength = 0;
        std::size_t Quantity = 1;
        std::array<bool, 2> DirectionAllowed{ true, true };
    };

    struct PairTypeGeometry final
    {
        PeriodicEndProfile Left;
        PeriodicEndProfile Right;
        AngleDomain RelativeRotationDomain;
    };

    enum class PairPoseStatus : std::uint8_t
    {
        InvalidInput = 0,
        Forbidden = 1,
        Unsupported = 2,
        BestFeasible = 3,
        CertifiedEpsilonOptimal = 4,
        BudgetExhausted = 5,
    };

    struct PairPoseResult final
    {
        PairPoseStatus Status = PairPoseStatus::InvalidInput;
        double RelativeRotationRadians = 0.0;
        double RequiredSeparationUpper = 0.0;
        double RequiredSeparationLower = 0.0;
        double OptimalityGap = (std::numeric_limits<double>::infinity)();
        std::size_t PhaseIntervalsEvaluated = 0;
        std::size_t AxialIntervalsEvaluated = 0;
        std::size_t FourierOrder = 0;
        ProfileSourceQuality SourceQuality = ProfileSourceQuality::Missing;
        std::string Diagnostic;
    };

    struct PairPoseSolverSettings final
    {
        /** Axial optimality target in Length units. */
        double Tolerance = 1.0;
        double FixedPhaseTolerance = 0.125;
        std::size_t MaximumPhaseIntervals = 65536;
        std::size_t MaximumAxialIntervals = 8192;
        std::size_t InitialContactCount = 8;
    };

    /** Solve one of the four forward/reverse pair combinations. */
    PairPoseResult SolveOrientedPairPose(
        const PairPartType& PartA_, const PairTypeGeometry& GeometryA_, Direction DirectionA_,
        const PairPartType& PartB_, const PairTypeGeometry& GeometryB_, Direction DirectionB_,
        const AngleDomain& Domain_, const PairPoseSolverSettings& Settings_ = {});

    /**
     * Compatibility bridge for the existing profile-recognition service.  It
     * creates spectra of the piecewise-linear periodic signal represented by
     * Samples_; native geometry services should provide NativeCertified levels
     * directly instead of using this helper.
     */
    PeriodicEndProfile BuildPeriodicProfileFromSamples(
        const std::vector<Length>& Samples_, double AxialOffset_ = 0.0,
        std::size_t MaximumOrder_ = 32, double SourceResidualUpper_ = 0.5);

    enum class PairEntryStatus : std::uint8_t
    {
        Missing = 0,
        Forbidden = 1,
        Feasible = 2,
        Certified = 3,
    };

    struct PairPoseMetadata final
    {
        double RelativeRotationRadians = 0.0;
        Length RequiredSeparation = 0;
        double ContinuousSeparationUpper = 0.0;
        double ContinuousSeparationLower = 0.0;
        double OptimalityGap = (std::numeric_limits<double>::infinity)();
        PairPoseStatus PoseStatus = PairPoseStatus::InvalidInput;
        ProfileSourceQuality SourceQuality = ProfileSourceQuality::Missing;
    };

    struct PairEntry final
    {
        Length NetSaving = 0;
        std::uint32_t PoseMetadataIndex = 0;
        PairEntryStatus Status = PairEntryStatus::Missing;
    };

    struct PairTableSnapshot final
    {
        std::string Schema = "icax.tube-pair-table.v1";
        std::string Version;
        std::string LengthUnit = "caller-defined-integer";
        std::string TypeDefinitionHash;
        std::string ProcessConfigurationHash;
        std::vector<PairPartType> Types;
        std::vector<PairEntry> Entries;
        std::vector<PairPoseMetadata> Poses;

        [[nodiscard]] std::size_t StateCount() const noexcept { return Types.size() * 2; }
        [[nodiscard]] std::size_t EntryIndex(
            std::size_t TypeA_, Direction DirectionA_,
            std::size_t TypeB_, Direction DirectionB_) const noexcept
        {
            const auto _A = TypeA_ * 2 + static_cast<std::size_t>(DirectionA_);
            const auto _B = TypeB_ * 2 + static_cast<std::size_t>(DirectionB_);
            return _A * StateCount() + _B;
        }
        [[nodiscard]] const PairEntry& At(
            std::size_t TypeA_, Direction DirectionA_,
            std::size_t TypeB_, Direction DirectionB_) const
        {
            return Entries.at(EntryIndex(TypeA_, DirectionA_, TypeB_, DirectionB_));
        }
    };

    struct PairTableBuildInput final
    {
        std::vector<PairPartType> Types;
        std::vector<PairTypeGeometry> Geometry;
        /** Optional T*T directed pair domains; empty uses Geometry[B].domain. */
        std::vector<AngleDomain> DirectedPairDomains;
        /** Optional 4*T*T domains using PairTableSnapshot::EntryIndex order. */
        std::vector<AngleDomain> OrientedPairDomains;
        /** Optional 4*T*T process losses, using PairTableSnapshot indexing. */
        std::vector<Length> PairProcessLosses;
        std::string Version;
        std::string TypeDefinitionHash;
        std::string ProcessConfigurationHash;
    };

    struct PairTableBuildSettings final
    {
        PairPoseSolverSettings Pose;
        std::size_t MaximumConcurrency = 0;
        bool RequireCertifiedProfiles = false;
    };

    PairTableSnapshot BuildPairTable(
        const PairTableBuildInput& Input_, const PairTableBuildSettings& Settings_ = {});

    bool ValidatePairTable(
        const PairTableSnapshot& Table_, std::vector<std::string>* Diagnostics_ = nullptr);

    /**
     * @brief 管材展开端部曲线的压缩表示。
     *
     * Period 是截面展开周长，Samples 是沿周向均匀采样的轴向偏移。采样坐标
     * 按 Period 周期循环，所以展开缝不会把同一条端曲线错误地切成两个形状。
     * 该结构只存排样所需的特征，不替代上游的 BRep/DXF/CSG 原始几何。
     */
    enum class CutLineFeatureKind : std::uint8_t
    {
        Invalid = 0,
        Constant = 1,
        Linear = 2,
        Sampled = 3,
    };

    struct CutLineFeatureCode final
    {
        std::string Schema = "icax.tube-cutline.v1";
        CutLineFeatureKind Kind = CutLineFeatureKind::Invalid;
        Length Period = 0;
        Length Minimum = 0;
        Length Maximum = 0;
        Length Mean = 0;
        Length TotalVariation = 0;
        Length LinearStart = 0;
        Length LinearEnd = 0;
        Length ConstantValue = 0;
        std::vector<Length> Samples;
        std::string ClassKey;
        std::uint64_t Fingerprint = 0;
    };

    /** 端曲线相位匹配的结果；RequiredSeparation 允许为负，表示安全套切重叠量。 */
    struct CutLineMatchResult final
    {
        bool Valid = false;
        Length RequiredSeparation = 0;
        Length PhaseOffset = 0;
        std::size_t Evaluations = 0;
    };

    /**
     * @brief 将均匀周向采样压缩为可索引的特征码。
     * @param Period_ 截面展开周长，必须与 Samples_ 使用同一长度单位。
     * @param Samples_ 相对端面基准的轴向偏移，可为负值。
     * @param LinearTolerance_ 识别常值/线性端曲线时允许的最大量化残差。
     */
    CutLineFeatureCode EncodeCutLineFeature(
        Length Period_, const std::vector<Length>& Samples_, Length LinearTolerance_ = 1);

    /** 判断编码是否可用于匹配；无效码会被求解器直接回退到旧的包络算法。 */
    bool IsValidCutLineFeature(const CutLineFeatureCode& Feature_);

    /** 在给定周向相位下计算一对端曲线的精确离散成本。 */
    CutLineMatchResult EvaluateCutLineMatch(
        const CutLineFeatureCode& Tail_, const CutLineFeatureCode& Head_, Length PhaseOffset_);

    /**
     * @brief 在一个有限相位网格上找最佳端曲线匹配。
     * @details 不会创建两两全量矩阵；调用方可以只对索引筛出的少量候选调用本函数。
     */
    CutLineMatchResult FindBestCutLineMatch(
        const CutLineFeatureCode& Tail_, const CutLineFeatureCode& Head_,
        std::size_t PhaseSamples_ = 64);

    /**
     * @brief 一个端部在排样阶段需要的几何摘要。
     * @details Signature 应由几何层在给定轴向、翻转状态和绕轴角度下规范化生成。
     *          两端只有在 AllowCommonCut=true 且 Signature 完全相同时才能共边。
     *          SeparationAllowance 表示非共边时该端额外需要的轴向安全量。
     */
    struct EndDescriptor
    {
        std::string Signature;
        Length SeparationAllowance = 0;
        bool AllowCommonCut = false;
        /** 斜端相对最内侧截面的轴向投影；为 0 时禁止梯形套切。 */
        Length NestingProjection = 0;
        /** 量化后的切割平面方向类别；相同类别的相邻斜端才允许互相嵌套。 */
        std::string NestingPlane;
        bool AllowTrapezoidNesting = false;
        // 新的展开条带端曲线编码。为空时保持旧版共刀/梯形算法行为。
        CutLineFeatureCode Feature;
    };

    /**
     * @brief 同一制造零件允许参与排样的一种姿态。
     * @details AxialLength 是该姿态下沿母材轴向的真实包围长度；为 0 时使用
     *          PartDemand::NominalLength。翻转和绕轴旋转均由上游显式枚举，求解器
     *          不猜测任意截面是否具备对称性。
     */
    struct PartVariant
    {
        std::string ID = "default";
        Length AxialLength = 0;
        /** 用于利用率统计的净材料等效长度；为 0 时使用 AxialLength。 */
        Length MaterialLength = 0;
        EndDescriptor LeftEnd;
        EndDescriptor RightEnd;
        bool Reversed = false;
        double RotationRadians = 0.0;
        // 沿截面展开周向的相位偏移，使用与 Period 相同的离散长度单位。
        Length PhaseOffset = 0;
    };

    /**
     * @brief 一类制造零件及其需求数量。
     */
    struct PartDemand
    {
        std::string ID;
        std::string CompatibilityKey;
        std::string OrderKey;
        Length NominalLength = 0;
        std::size_t Quantity = 1;
        std::vector<PartVariant> Variants;
    };

    enum class StockKind
    {
        Standard,
        Remnant
    };

    /**
     * @brief 可用母材或余料规格。
     * @details Quantity=0 表示不限数量，仅建议用于标准定尺料。CostUnits<0 时，
     *          标准料默认按整根长度计成本，余料默认成本为 0；调用方也可传入采购
     *          成本或余料机会成本。FrontMargin 是前端固定损耗，TailDeadZone 是末件
     *          之后必须保留的最短夹持长度，MinReusableRemainder 决定尾料是否入库。
     */
    struct StockType
    {
        std::string ID;
        std::string CompatibilityKey;
        StockKind Kind = StockKind::Standard;
        Length TotalLength = 0;
        std::size_t Quantity = 0;
        std::int64_t CostUnits = -1;
        Length FrontMargin = 0;
        Length TailDeadZone = 0;
        Length MinReusableRemainder = 0;
        std::size_t MaxParts = 0;
        std::size_t MaxDistinctOrders = 0;

        /**
         * Optional identity and fixed-prefix state for an already opened stock
         * instance.  When FixedInstanceID is non-empty, the stock is available
         * exactly once, InitialProcessedEnd is the absolute end of its immutable
         * prefix, and only newly appended placements are returned by the solver.
         *
         * InitialPredecessorTypeID names the last locked part in the pair table.
         * Its directed pair entry supplies the saving and relative roll for the
         * first appended part.  Leaving it empty represents a flat/open boundary
         * that receives no start saving.  InitialPredecessorType is resolved by
         * PairTableSolver and must not be persisted by callers.
         */
        std::string SourceTypeID;
        std::string FixedInstanceID;
        Length InitialProcessedEnd = 0;
        std::string InitialPredecessorTypeID;
        Direction InitialPredecessorDirection = Direction::Forward;
        double InitialPredecessorRotationRadians = 0.0;
        std::size_t InitialPredecessorType = (std::numeric_limits<std::size_t>::max)();
    };

    enum class Objective
    {
        StockCost,
        StandardStockCount,
        UnrecoverableWaste,
        OpenedStockCount,
        ReusableRemainderCount,
        CutCount,
        OrderChangeCount
    };

    struct SolverSettings
    {
        /** 每次锯切消耗的锯缝宽度。 */
        Length Kerf = 0;
        /** 两个不能共用同一刀的端面之间，除两次锯缝外还需丢弃的安全间隔。 */
        Length PartGap = 0;
        std::size_t ConstructionRuns = 8;
        std::size_t LocalSearchPasses = 6;
        std::size_t LargeNeighborhoodIterations = 160;
        std::size_t ExactPartLimit = 10;
        std::uint64_t ExactNodeLimit = 250000;
        std::uint32_t RandomSeed = 0x1CA7U;
        std::vector<Objective> ObjectiveOrder{
            Objective::StockCost,
            Objective::StandardStockCount,
            Objective::UnrecoverableWaste,
            Objective::OpenedStockCount,
            Objective::ReusableRemainderCount,
            Objective::CutCount,
            Objective::OrderChangeCount,
        };
    };

    struct Placement
    {
        std::string DemandID;
        std::string InstanceID;
        std::string VariantID;
        std::string OrderKey;
        Length Start = 0;
        Length End = 0;
        Length GapBefore = 0;
        bool Reversed = false;
        double RotationRadians = 0.0;
        Length PhaseOffset = 0;
        bool CommonCutWithPrevious = false;
        Direction PartDirection = Direction::Forward;
        Length SavingFromPrevious = 0;
        std::uint32_t PairPoseMetadataIndex = 0;
    };

    struct StockPlan
    {
        std::string StockTypeID;
        std::string StockInstanceID;
        std::string CompatibilityKey;
        StockKind Kind = StockKind::Standard;
        bool Continued = false;
        Length TotalLength = 0;
        Length FrontMargin = 0;
        Length TailDeadZone = 0;
        Length InitialProcessedEnd = 0;
        Length ProcessedEnd = 0;
        Length RemainingLength = 0;
        Length ReusableRemainderLength = 0;
        Length UnrecoverableWaste = 0;
        Length PartLength = 0;
        Length GapLength = 0;
        std::size_t CutCount = 0;
        std::size_t CommonCutCount = 0;
        std::vector<Placement> Placements;
        /** Sum(L)-sum(S), excluding fixed stock margins. */
        Length OccupiedLength = 0;
    };

    struct ResultMetrics
    {
        std::int64_t StockCost = 0;
        std::size_t StandardStockCount = 0;
        std::size_t OpenedStockCount = 0;
        Length TotalStockLength = 0;
        Length PartLength = 0;
        Length GapLength = 0;
        Length UnrecoverableWaste = 0;
        Length ReusableRemainderLength = 0;
        std::size_t ReusableRemainderCount = 0;
        std::size_t CutCount = 0;
        std::size_t CommonCutCount = 0;
        std::size_t OrderChangeCount = 0;
        double GrossUtilization = 0.0;
        double NetMaterialYield = 0.0;
        std::uint64_t SearchNodes = 0;
        /** 仅当全部兼容组都能化为顺序无关切割模式时有效。 */
        bool QualityLowerBoundAvailable = false;
        std::int64_t StockCostLowerBound = 0;
        std::size_t OpenedStockCountLowerBound = 0;
        double StockCostRelativeGap = 0.0;
        double OpenedStockCountRelativeGap = 0.0;
    };

    enum class SolveStatus
    {
        InvalidInput,
        Infeasible,
        Feasible,
        Optimal,
        /** Search ended without an incumbent and without an infeasibility proof. */
        NoSolutionFound,
    };

    enum class TerminationReason : std::uint8_t
    {
        Completed,
        TimeLimit,
        EvaluationLimit,
        Cancelled,
        StateLimit,
        InvalidInput,
        Unsupported,
    };

    enum class ProofScope : std::uint8_t
    {
        None,
        RestrictedNeighborhood,
        FixedOrder,
        FullIntegerModel,
    };

    struct SolveResult
    {
        SolveStatus Status = SolveStatus::InvalidInput;
        bool ExactSearchCompleted = false;
        ResultMetrics Metrics;
        std::vector<StockPlan> Stocks;
        std::vector<std::string> UnplacedInstances;
        std::vector<std::string> Diagnostics;
        TerminationReason Termination = TerminationReason::Completed;
        ProofScope ProvenScope = ProofScope::None;
        bool FixedOrderSplitOptimal = false;
        std::string PairTableVersion;
    };

    enum class PairObjectivePolicy : std::uint8_t
    {
        MinPurchasedLength,
        MinPurchaseCost,
    };

    enum class ConstraintMode : std::uint8_t
    {
        Disabled,
        FullExact,
        HybridLargeNeighborhood,
    };

    struct PairTableSolverSettings final
    {
        PairObjectivePolicy ObjectivePolicy = PairObjectivePolicy::MinPurchasedLength;
        ConstraintMode Constraint = ConstraintMode::HybridLargeNeighborhood;
        std::size_t ConstructionRuns = 6;
        std::size_t LocalSearchPasses = 3;
        std::size_t CandidateWidth = 32;
        std::size_t LargeNeighborhoodIterations = 32;
        std::size_t ExactPartLimit = 9;
        std::uint64_t ExactNodeLimit = 250000;
        std::size_t FiniteInventoryLabelLimit = 200000;
        std::uint64_t EvaluationLimit = 2000000;
        /** CP-SAT budget for one complete group or one dynamic neighborhood. */
        std::uint64_t ConstraintTimeBudgetMilliseconds = 2000;
        /** Zero selects a bounded hardware-derived worker count. */
        std::size_t ConstraintWorkers = 0;
        /** Maximum number of released instances in one CP-SAT neighborhood. */
        std::size_t ConstraintNeighborhoodPartLimit = 9;
        /** Independent wall-clock budget for one released neighborhood. */
        std::uint64_t ConstraintNeighborhoodTimeBudgetMilliseconds = 100;
        std::uint64_t TimeBudgetMilliseconds = 0;
        std::uint32_t RandomSeed = 0x1CA7U;
    };

    /**
     * Production table-driven nesting service.  It never evaluates BRep/end
     * curves: every adjacency is read from the immutable four-pose table.
     */
    class PairTableSolver final
    {
    public:
        [[nodiscard]] SolveResult Solve(
            const PairTableSnapshot& Table_,
            const std::vector<StockType>& Stocks_,
            const PairTableSolverSettings& Settings_ = {}) const;
    };

    /**
     * @brief 面向管材/型材锯切的一维、带姿态和邻接约束的混合排样求解器。
     * @details 大组使用多起点最佳插入、局部搜索和大邻域重排；小组继续执行有节点
     *          上限的完整分支搜索。若完整搜索未触达节点上限，结果标记为 Optimal。
     */
    class Solver
    {
    public:
        [[nodiscard]] SolveResult Solve(
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings = {}) const;
    };
}
