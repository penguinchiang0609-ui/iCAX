#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维直线段
        * @details
        * Segment2 表示局部坐标系下的一条二维有界线段。
        * - 参数化区间 u ∈ [0,1]，u=0 对应起点，u=1 对应终点。
        * - 可以通过 Transform() 对线段进行二维仿射变换。
        * - 支持反转 Reverse()，交换起点和终点。
        * - 可判断是否闭合 IsClosed()。
        * - 对于直线段，Orientation() 返回 kCurveUnknown2，因为直线段无朝向概念。
        */
        class _GEOMETRY_EXP Segment2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系
            * @param [in] ptStart_ 起点
            * @param [in] ptEnd_ 终点
            */
            Segment2(IN const Point2& ptStart_, IN const Point2& ptEnd_);

            /**
            * @brief 析构函数
            */
            virtual ~Segment2();

        public: //!< 属性访问接口
            /**
            * @brief 设置起点和终点
            * @param [in] ptStart_ 起点
            * @param [in] ptEnd_ 终点
            */
            void SetPoints(const Point2& ptStart_, const Point2& ptEnd_);

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
            double m_nLength;      //!< 线段长度
            bool m_bReversed;
        };
    }
}
