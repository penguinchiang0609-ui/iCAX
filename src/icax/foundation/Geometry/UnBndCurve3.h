#pragma once
#include "GeometryPrimative.h"
#include "Curve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维无界曲线抽象基类
        * @details
        * UnBndCurve3 表示在无限参数区间 (-∞, +∞) 或半无限区间 [0, +∞) 上定义的三维参数曲线。
        * 它继承自 Curve3，并明确声明了“无界曲线”的特性。
        *
        * 常见无界曲线示例：
        * - Line3：无限延伸的直线
        * - Ray3：起点固定、沿某方向无限延伸的射线
        *
        * @note
        * 1. 本类仅提供无界曲线的几何语义，数值算法（如求交、投影等）应在 GeometryAlgo 层中实现；
        * 2. 参数区间与 Evaluate(u) 的几何定义由具体子类决定；
        * 3. 无界曲线默认非周期、无边界。
        */
        class _GEOMETRY_EXP UnBndCurve3 : public Curve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系
            * @details
            * 局部坐标系定义了曲线的空间位置与方向基准。
            */
            explicit UnBndCurve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~UnBndCurve3();

        public: //!< Curve3 接口实现
            /**
            * @brief 获取曲线边界类型
            * @return CurveBndType3::kNoneBnd2
            * @details
            * 无界曲线的参数区间通常为：
            * - (-∞, +∞)：无限直线；
            * - [0, +∞)：射线。
            */
            virtual CurveBndType3 BndType() const override final;

            /**
            * @brief 获取曲线起始参数
            * @return -∞
            * @details
            * 对于完全无界曲线，起始参数定义为负无穷。
            * 对于半无界曲线（如射线），由子类重载该函数。
            */
            virtual double StartParam() const override final;

            /**
            * @brief 获取曲线终止参数
            * @return +∞
            * @details
            * 对于完全无界曲线，终止参数定义为正无穷。
            * 对于半无界曲线（如射线），由子类重载该函数。
            */
            virtual double EndParam() const override final;

            /**
            * @brief 参数归一化
            * @param [in] nU_ 输入参数值
            * @return 归一化参数
            * @details
            * 无界曲线不具备有限参数区间，本函数直接返回输入参数。
            */
            virtual double NormalizeParam(IN const double& nU_) const override final;

            /**
            * @brief 判断曲线是否为周期曲线
            * @return false
            * @details
            * 无界曲线不封闭，默认非周期。
            */
            virtual bool IsPeriodic() const override final;

            /**
            * @brief 获取曲线周期
            * @return 0.0
            * @details
            * 无界曲线无周期性，恒返回 0。
            */
            virtual double Period() const override final;
        };
    }
}
