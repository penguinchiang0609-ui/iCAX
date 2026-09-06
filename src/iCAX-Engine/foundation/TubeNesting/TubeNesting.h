#pragma once

#include <cstddef>
#include <cstdint>
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
        bool CommonCutWithPrevious = false;
    };

    struct StockPlan
    {
        std::string StockTypeID;
        std::string StockInstanceID;
        std::string CompatibilityKey;
        StockKind Kind = StockKind::Standard;
        Length TotalLength = 0;
        Length FrontMargin = 0;
        Length TailDeadZone = 0;
        Length ProcessedEnd = 0;
        Length RemainingLength = 0;
        Length ReusableRemainderLength = 0;
        Length UnrecoverableWaste = 0;
        Length PartLength = 0;
        Length GapLength = 0;
        std::size_t CutCount = 0;
        std::size_t CommonCutCount = 0;
        std::vector<Placement> Placements;
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
        Optimal
    };

    struct SolveResult
    {
        SolveStatus Status = SolveStatus::InvalidInput;
        bool ExactSearchCompleted = false;
        ResultMetrics Metrics;
        std::vector<StockPlan> Stocks;
        std::vector<std::string> UnplacedInstances;
        std::vector<std::string> Diagnostics;
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
