#pragma once
#include "GeometryPrimative.h"
#include "Curve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维半有界曲线抽象基类
        * @details
        * HalfBndCurve3 表示在半无限参数区间上定义的三维曲线，
        * 通常为 \f$ [0, +\infty) \f$。
        *
        * 它扩展了 Curve3，增加了对“半有界”特性的语义表达。
        *
        * 典型的半有界曲线包括：
        * - Ray3（射线）
        * - 半抛物线等
        *
        * @note
        * 本类仅描述几何意义上的“半有界曲线”；
        * 不提供求交、投影、参数反解等数值算法。
        * 这些应在 GeometryAlgo 层中实现。
        */
        class _GEOMETRY_EXP HalfBndCurve3 : public Curve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 曲线的局部坐标系
            */
            explicit HalfBndCurve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~HalfBndCurve3();

        public: //!< Curve3 接口实现

            /**
            * @brief 获取曲线边界类型
            * @return CurveBndType3::kHalfBnd2
            * @details
            * 半有界曲线通常定义在 [0, +∞) 区间。
            */
            virtual CurveBndType3 BndType() const override final;

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart = 0.0
            */
            virtual double StartParam() const override final;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd = +∞
            */
            virtual double EndParam() const override final;

            /**
            * @brief 参数归一化
            * @param [in] nU_ 原始参数值
            * @return 归一化到 [0, +∞) 范围的参数值
            */
            virtual double NormalizeParam(IN const double& nU_) const override final;

            /**
            * @brief 是否为周期曲线
            * @return false
            * @details
            * 半有界曲线不会周期性重复，恒为 false。
            */
            virtual bool IsPeriodic() const override final;

            /**
            * @brief 返回曲线周期
            * @return 0.0
            * @details
            * 半有界曲线无周期，恒返回 0。
            */
            virtual double Period() const override final;
        };
    }
}
