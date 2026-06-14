#pragma once
#include "GeometryPrimative.h"
#include "Curve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维有界曲线抽象基类
        * @details
        * BndCurve2 表示在有限参数区间 [uStart, uEnd] 上定义的二维曲线。
        * 它扩展了 Curve2，增加了起止参数、端点及闭合性等属性。
        *
        * 典型的有界曲线包括：
        * - LineSegment2（线段）
        * - Arc2（圆弧）
        * - Circle2（完整圆）
        * - Bezier2 / BSpline2（样条段）
        *
        * @note
        * 本类不提供根据点反算参数 u 的功能，因为：
        * 1. 对于 Bezier / BSpline 曲线，该计算涉及复杂的数值方法；
        * 2. 曲线可能自交，导致结果不唯一；
        * 3. 该方法属于算法范畴，应在 GeometryAlgo 层实现。
        */
        class _GEOMETRY_EXP BndCurve2 : public Curve2
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] csLocal_ 自身坐标系
            */
            BndCurve2(IN const CoordSys2& csLocal_);

            /**
            * @brief 析构函数（默认）
            */
            virtual ~BndCurve2();

        public: //!< 有界曲线基础 接口
            /**
            * @brief 获取曲线起点坐标
            * @return 曲线起点的二维坐标
            */
            virtual Point2 StartPoint() const;

            /**
            * @brief 获取曲线终点坐标
            * @return 曲线终点的二维坐标
            */
            virtual Point2 EndPoint() const;

            /**
            * @brief 判断曲线是否闭合
            * @param [in] nTol_ 容差值（默认 EPSILON）
            * @return 若首尾点间距小于 tol_，则视为闭合
            * @details
            * 对于如 Circle2、ClosedBezier2 等类型，此函数始终返回 true。
            */
            virtual bool IsClosed(IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 获取曲线朝向类型
            * @return BndCurve2Orientation
            */
            virtual CurveOrientType Orientation() const;

        public: //!< Curve2 接口实现
            /**
            * @brief 获取曲线边界类型
            * @details
            * 有界曲线具有有限的参数范围，例如线段、圆弧、样条段；
            * 无界曲线（如直线、射线）则参数区间为无穷。
            * @return CurveBndType2
            */
            virtual CurveBndType2 BndType() const override final;

            /*
            * @brief 参数归一化
            * @param [in] nU_double
            * @return
            */
            virtual double NormalizeParam(IN const double& nU_) const override final;

            /*
            * @brief 是否周期性
            * @return bool
            */
            virtual bool IsPeriodic() const override final;

            /*
            * @brief 周期
            * @return bool
            */
            virtual double Period() const override final;
        };
    }
}
