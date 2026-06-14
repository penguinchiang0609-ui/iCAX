#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维二阶贝塞尔曲线
        * @details
        * QuadraticBezier2 表示定义在局部坐标系内的二维二阶开口贝塞尔曲线。
        *
        * - 曲线由 3 个控制点定义：起点、控制点、终点；
        * - 支持通过 Evaluate() 计算曲线上任意参数点；
        * - 支持计算切向和反转方向；
        * - 可通过 Transform() 接口应用二维仿射变换。
        *
        * 曲线参数 u 统一归一化在 [0, 1] 区间：
        * - u=0 对应起点；
        * - u=1 对应终点；
        * - Evaluate(u) 返回曲线上对应点坐标。
        */
        class _GEOMETRY_EXP QuadraticBezier2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] pt0_ 起点
            * @param [in] pt1_ 控制点
            * @param [in] pt2_ 终点
            */
            QuadraticBezier2(IN const Point2& pt0_, IN const Point2& pt1_, IN const Point2& pt2_);

            /**
            * @brief 默认析构函数
            */
            virtual ~QuadraticBezier2();

        public: //!< 控制点访问
            /*
            * @brief 起点
            * @return const Point2&
            */
            const Point2& P0() const;

            /*
            * @brief 控制点1
            * @return const Point2&
            */
            const Point2& P1() const;

            /*
            * @brief 终点
            * @return const Point2&
            */
            const Point2& P2() const;

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
            * @brief 设置终点2
            * @param [in] pt_
            */
            void SetP2(IN const Point2& pt_);

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
            Point2 m_pt1; //!< 控制点
            Point2 m_pt2; //!< 终点
        };
    }
}
