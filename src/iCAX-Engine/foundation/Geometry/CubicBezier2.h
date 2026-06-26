#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三阶贝塞尔曲线（二维）
        * @details
        * CubicBezier2 表示二维三阶贝塞尔曲线，定义在局部坐标系中。
        *
        * - 曲线通过起点、终点和两个控制点定义；
        * - 参数 u 归一化到 [0,1]；
        * - 提供 Evaluate() 和 Tangent() 方法计算曲线上点和切向；
        * - 可通过 Reverse() 方法反转曲线方向；
        * - 可通过 Transform() 方法执行二维仿射变换。
        *
        * 三阶贝塞尔曲线公式：
        * \f[
        * P(u) = (1-u)^3 P_0 + 3(1-u)^2 u P_1 + 3(1-u) u^2 P_2 + u^3 P_3, \quad u \in [0,1]
        * \f]
        */
        class _GEOMETRY_EXP CubicBezier2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] pt0_ 起点
            * @param [in] pt1_ 控制点1
            * @param [in] pt2_ 控制点2
            * @param [in] pt3_ 终点
            */
            CubicBezier2(IN const Point2& pt0_, IN const Point2& pt1_, IN const Point2& pt2_, IN const Point2& pt3_);

            /**
            * @brief 默认析构函数
            */
            virtual ~CubicBezier2();

        public: //!< 控制点访问接口

            /**
            * @brief 获取起点
            * @return 起点 P0
            */
            const Point2& P0() const;

            /**
            * @brief 获取控制点1
            * @return 控制点 P1
            */
            const Point2& P1() const;

            /**
            * @brief 获取控制点2
            * @return 控制点 P2
            */
            const Point2& P2() const;

            /**
            * @brief 获取终点
            * @return 终点 P3
            */
            const Point2& P3() const;

            /*
            * @brief 设置起点
            * @param [in] pt_
            */
            void SetP0(IN const Point2& pt_);

            /*
            * @brief 设置控制点1
            * @param [in] pt_
            */
            void SetP1(IN const Point2& pt_);

            /*
            * @brief 设置控制点2
            * @param [in] pt_
            */
            void SetP2(IN const Point2& pt_);

            /*
            * @brief 设置终点
            * @param [in] pt_
            */
            void SetP3(IN const Point2& pt_);

        public://!< BndCurve2 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const override;

        public://!< Curve2 接口
            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            * @note 参数范围由具体曲线类型定义，例如 [0,1] 或 [0,2π]。
            */
            virtual Point2 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

        public: //!< Geometry2 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            Point2 m_pt0; //!< 起点
            Point2 m_pt1; //!< 控制点1
            Point2 m_pt2; //!< 控制点2
            Point2 m_pt3; //!< 终点
        };
    }
}
