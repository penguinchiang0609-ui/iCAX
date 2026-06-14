#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"
#include <memory>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维带权 B 样条曲线（NURBS）
        * @details
        * NURBS2 表示一个二维带权 B 样条曲线，可通过控制点、权重和节点向量定义。
        *
        * 曲线定义：
        * \f[
        * C(u) = \frac{\sum_{i=0}^{n} N_{i,p}(u) w_i P_i}{\sum_{i=0}^{n} N_{i,p}(u) w_i}, \quad u \in [u_0, u_m]
        * \f]
        *
        * 其中：
        * - \(P_i\) 为控制点
        * - \(w_i\) 为权重
        * - \(N_{i,p}(u)\) 为 B 样条基函数，阶数为 \(p\)
        * - 节点向量长度为 n+p+2
        *
        * 特性：
        * - 支持任意阶 B 样条（p >= 1）
        * - 可通过 Transform() 接口应用二维仿射变换
        * - 支持参数化 Evaluate、Tangent 以及 Reverse 操作
        */
        class _GEOMETRY_EXP NURBS2 : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstControlPoints_ 控制点列表
            * @param [in] lstWeights_ 对应控制点的权重列表
            * @param [in] lstKnotVector_ 节点向量
            * @param [in] nDegree_ 样条阶数（p >= 1）
            * @note 权重必须与控制点一一对应，节点向量长度应为 controlPoints_.size() + degree_ + 1
            */
            NURBS2(IN const std::vector<Point2>& lstControlPoints_,
                IN const std::vector<double>& lstWeights_,
                IN const std::vector<double>& lstKnotVector_,
                IN const int nDegree_);

            /**
            * @brief 默认析构函数
            */
            virtual ~NURBS2();

        public: //!< 基础访问接口
            /**
            * @brief 获取控制点列表
            */
            const std::vector<Point2>& ControlPoints() const;

            /**
            * @brief 获取权重列表
            */
            const std::vector<double>& Weights() const;

            /**
            * @brief 获取节点向量
            */
            const std::vector<double>& KnotVector() const;

            /**
            * @brief 获取曲线阶数
            */
            int Degree() const;

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
            std::vector<Point2> m_lstControlPoints; //!< 控制点列表
            std::vector<double> m_lstWeights;        //!< 权重列表
            std::vector<double> m_lstKnots;     //!< 节点向量
            int m_nDegree;                         //!< 曲线阶数
        };
    }
}
