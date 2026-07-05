#pragma once
#include "GeometryPrimative.h"
#include "Curve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维有界曲线抽象基类
        * @details
        * BndCurve3 表示在有限参数区间 `[uStart, uEnd]` 上定义的三维曲线。
        * 它扩展了 Curve3，增加了起止参数、端点及闭合性等属性。
        *
        * 典型的有界曲线包括：
        * - LineSegment3（线段）
        * - Arc3（圆弧）
        * - Circle3（完整圆）
        * - Bezier3 / BSpline3（样条段）
        *
        * @note
        * 本类不提供根据点反算参数 u 的功能，因为：
        * 1. 对于 Bezier / BSpline 曲线，该计算涉及复杂的数值方法；
        * 2. 曲线可能自交，导致结果不唯一；
        * 3. 此类功能属于算法范畴，应在 GeometryAlgo 层实现。
        */
        class _GEOMETRY_EXP BndCurve3 : public Curve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 曲线的局部坐标系
            */
            explicit BndCurve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 析构函数（默认）
            */
            virtual ~BndCurve3();

        public: //!< 有界曲线特性接口
            /**
            * @brief 获取曲线起点坐标
            * @return 起点的三维坐标
            */
            virtual Point3 StartPoint() const;

            /**
            * @brief 获取曲线终点坐标
            * @return 终点的三维坐标
            */
            virtual Point3 EndPoint() const;

            /**
            * @brief 判断曲线是否闭合
            * @param [in] nTol_ 容差（默认 EPSILON）
            * @return 若首尾点间距小于 nTol_，则视为闭合
            * @details
            * 对于如 Circle3、ClosedBezier3 等类型，此函数恒返回 true。
            */
            virtual bool IsClosed(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 获取曲线朝向类型
            * @param [in] dirNormal_ 法向
            * @return BndCurve2Orientation
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const;

        public: //!< Curve3 接口实现
            /**
            * @brief 获取曲线边界类型
            * @return CurveBndType3::Bounded
            * @details
            * 有界曲线具有有限参数范围；
            * 无界曲线（如 Line3、Ray3）则为 Unbounded。
            */
            virtual CurveBndType3 BndType() const override final;

            /**
            * @brief 参数归一化
            * @param [in] nU_ 原始参数
            * @return 归一化参数（映射到 [0,1]）
            * @details
            * 将曲线参数区间 [uStart, uEnd] 线性映射为 [0,1]。
            */
            virtual double NormalizeParam(IN const double& nU_) const override final;

            /**
            * @brief 是否为周期曲线
            * @return false（默认）
            * @details
            * 对于如 Circle3，此函数应返回 true；
            * 否则，默认返回 false。
            */
            virtual bool IsPeriodic() const override;

            /**
            * @brief 返回曲线周期长度
            * @return 周期长度，非周期曲线返回 0.0
            */
            virtual double Period() const override;
        };
    }
}
