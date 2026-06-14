#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"
#include <memory>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维带权 B 样条曲线（NURBS）
        * @details
        * NURBS3 表示一个三维非均匀有理 B 样条曲线（Non-Uniform Rational B-Spline），
        * 通过控制点、权重和节点向量共同定义，广泛应用于 CAD/CAM 建模、曲面拟合、
        * 以及复杂路径的精确控制。
        *
        * 数学定义：
        * \f[
        * C(u) = \frac{\sum_{i=0}^{n} N_{i,p}(u) \, w_i \, P_i}{\sum_{i=0}^{n} N_{i,p}(u) \, w_i}, \quad u \in [u_0, u_m]
        * \f]
        *
        * 其中：
        * - \( P_i \)：控制点；
        * - \( w_i \)：权重；
        * - \( N_{i,p}(u) \)：B 样条基函数，阶数为 \( p \)；
        * - 节点向量长度为 \( n + p + 2 \)。
        *
        * 特性：
        * - 支持任意阶 B 样条（p ≥ 1）；
        * - 权重控制曲线趋近控制点的程度（权重大时曲线靠近该点）；
        * - 可通过 Transform() 接口应用三维仿射变换；
        * - 支持参数化 Evaluate、Tangent、Reverse 等几何操作。
        */
        class _GEOMETRY_EXP NURBS3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstControlPoints_ 控制点列表
            * @param [in] lstWeights_ 对应控制点的权重列表
            * @param [in] lstKnotVector_ 节点向量
            * @param [in] nDegree_ 样条阶数（p ≥ 1）
            * @note 权重数量必须与控制点数量一致；
            *       节点向量长度应满足 controlPoints_.size() + degree_ + 1。
            * @throw std::invalid_argument 当输入参数维度不匹配时抛出异常。
            */
            NURBS3(IN const std::vector<Point3>& lstControlPoints_,
                IN const std::vector<double>& lstWeights_,
                IN const std::vector<double>& lstKnotVector_,
                IN const int nDegree_);

            /**
            * @brief 析构函数
            */
            virtual ~NURBS3();

        public: //!< 属性访问
            /**
            * @brief 获取控制点列表
            * @return 控制点数组（常量引用）
            */
            const std::vector<Point3>& ControlPoints() const;

            /**
            * @brief 获取权重列表
            * @return 权重数组（常量引用）
            */
            const std::vector<double>& Weights() const;

            /**
            * @brief 获取节点向量
            * @return 节点向量数组（常量引用）
            */
            const std::vector<double>& KnotVector() const;

            /**
            * @brief 获取曲线阶数
            * @return 曲线阶数（p）
            */
            int Degree() const;

        public: //!< Curve3 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间由节点向量定义。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const override;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的三维点坐标
            * @note 参数范围由节点向量定义，通常为 [u_p, u_{m-p}]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即参数 u 的增大方向反转。
            */
            virtual void Reverse() override;

            /**
            * @brief 获取圆弧所在平面
            * @return 若共面，返回对应的 Plane3；否则返回 std::nullopt
            */
            virtual std::optional<Plane3> SuggestPlane() const override;

            /*
            * @brief 判断是否在指定平面内
            * @param [in] pln_
            * @return bool
            */
            virtual bool IsOnPlane(IN const Plane3& pln_) const override;

        public: //!< Geometry3 接口实现
            /**
            * @brief 缩放与剪切变换
            * @param [in] nScale_ 缩放系数
            * @param [in] nShear_ 剪切系数
            * @param [in] bMirrorX_ 是否在 X 轴方向镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前曲线对象
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            std::vector<Point3> m_lstControlPoints; //!< 控制点列表
            std::vector<double> m_lstWeights;       //!< 权重列表
            std::vector<double> m_lstKnots;         //!< 节点向量
            int m_nDegree;                          //!< 曲线阶数
        };
    }
}
