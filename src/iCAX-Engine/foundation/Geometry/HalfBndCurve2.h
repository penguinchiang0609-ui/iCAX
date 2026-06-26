#pragma once
#include "GeometryPrimative.h"
#include "Curve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维无界曲线抽象基类
        * @details
        * HalfBndCurve2 表示在无限参数区间 (-∞, +∞) 或半无限区间上定义的二维曲线。
        * 它扩展了 Curve2，增加了对无界特性的语义表达。
        *
        * 典型的无界曲线包括：
        * - Line2（无限直线）
        * - Ray2（射线）
        *
        * @note
        * 本类只描述几何意义上的“无界曲线”；
        * 不提供求交、投影、参数反解等数值算法。
        * 这些应在 GeometryAlgo 层中实现。
        */
        class _GEOMETRY_EXP HalfBndCurve2 : public Curve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 曲线的局部坐标系
            */
            explicit HalfBndCurve2(IN const CoordSys2& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~HalfBndCurve2();

        public: //!< Curve2 接口实现
            /**
            * @brief 获取曲线边界类型
            * @return CurveBndType2::Unbounded
            * @details
            * 无界曲线的参数范围通常为：
            * - (-∞, +∞)：如无限直线；
            * - [0, +∞)：如射线。
            */
            virtual CurveBndType2 BndType() const override final;

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            virtual double StartParam() const override final;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const override final;

            /*
            * @brief 参数归一化
            * @param [in] nU_double
            * @return
            */
            virtual double NormalizeParam(IN const double& nU_) const override final;

            /**
            * @brief 是否为周期曲线
            * @return false
            * @details
            * 无界曲线永不封闭，默认非周期。
            */
            virtual bool IsPeriodic() const override final;

            /**
            * @brief 返回曲线周期
            * @return 0.0
            * @details
            * 无界曲线无周期，恒返回 0。
            */
            virtual double Period() const override final;
        };
    }
}
