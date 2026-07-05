#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维 Catmull-Rom 样条曲线
        * @details
        * CatmullRomSpline2 表示一种通过所有控制点的平滑插值曲线，
        * 适合用于路径平滑、CAM 刀具轨迹插值等。
        *
        * 曲线定义为分段样条，每段由 4 个相邻控制点决定：
        * \f[
        * P_i(t) = 0.5 \cdot \left[
        * (2 P_i) + (-P_{i-1} + P_{i+1}) t + (2P_{i-1} - 5P_i + 4P_{i+1} - P_{i+2}) t^2 + (-P_{i-1} + 3P_i - 3P_{i+1} + P_{i+2}) t^3
        * \right]
        * \f]
        * 其中 tension 参数控制曲率光顺程度。
        *
        * @note
        * - 当 tension = 0 时为标准 Catmull-Rom；
        * - 当 tension = 1 时退化为折线；
        * - tension < 0 时曲线更平滑；
        */
        class _GEOMETRY_EXP CatmullRomSpline2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstCtrlPts_ 控制点序列（至少 4 个）
            * @param [in] nTension_ 张力系数（默认 0.0）
            */
            CatmullRomSpline2(IN const std::vector<Point2>& lstCtrlPts_, IN const double& nTension_ = 0.0);

            /**
            * @brief 析构函数
            */
            virtual ~CatmullRomSpline2();

        public: //!< 属性访问
            /**
            * @brief 获取控制点列表
            * @return 控制点序列（常量引用）
            */
            const std::vector<Point2>& ControlPoints() const;

            /**
            * @brief 获取张力系数
            */
            const double& Tension() const;

            /**
            * @brief 设置张力系数
            * @param [in] nTension_
            */
            void SetTension(IN const double& nTension_);

            /*
            * @brief 设置控制点列表
            * @param [in] lstCtrlPts_
            */
            void SetCtrlPoints(IN const std::vector<Point2>& lstCtrlPts_);

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
            std::vector<Point2> m_lstCtrlPts; //!< 控制点序列
            double m_nTension;                //!< 张力系数
        };
    }
}
