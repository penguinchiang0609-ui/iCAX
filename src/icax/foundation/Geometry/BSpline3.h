#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"
#include <vector>
#include <memory>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维 B 样条曲线（非有理形式）
        * @details
        * BSpline3 表示定义在局部坐标系内的三维普通 B 样条曲线，
        * 由控制点、节点向量以及阶数（Degree）共同定义。
        *
        * - 曲线不携带权重信息（即非 NURBS）；
        * - 支持任意维度的仿射变换；
        * - 控制点数量为 n+1，节点向量长度应满足 n + degree + 2；
        *
        * 数学定义：
        * \f[
        * C(u) = \sum_{i=0}^{n} N_{i,p}(u) P_i
        * \f]
        * 其中 \f$N_{i,p}(u)\f$ 为 p 阶 B 样条基函数，\f$P_i\f$ 为控制点。
        */
        class _GEOMETRY_EXP BSpline3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstControlPoints_ 控制点序列
            * @param [in] lstKnotVector_ 节点向量
            * @param [in] nDegree_ 曲线阶数（p ≥ 1）
            * @throw std::invalid_argument 当节点向量长度不满足 n + degree + 2 时抛出
            */
            BSpline3(IN const std::vector<Point3>& lstControlPoints_,
                IN const std::vector<double>& lstKnotVector_,
                IN const int& nDegree_);

            /**
            * @brief 析构函数
            */
            virtual ~BSpline3();

        public: //!< 基础属性访问
            /**
            * @brief 获取控制点序列
            * @return 控制点数组引用
            */
            const std::vector<Point3>& ControlPoints() const;

            /**
            * @brief 获取曲线阶数
            * @return 曲线阶数（p）
            */
            int Degree() const;

            /**
            * @brief 获取节点向量
            * @return 节点向量数组引用
            */
            const std::vector<double>& KnotVector() const;

        public: //!< Curve3 接口实现
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 通常等于节点向量中的首个非重复节点值。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常等于节点向量中的最后一个非重复节点值。
            */
            virtual double EndParam() const override;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的三维点坐标
            * @note 参数范围通常为 [StartParam(), EndParam()]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原定义方向相反。
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
            * @param [in] nScale_ 缩放向量
            * @param [in] nShear_ 剪切参数
            * @param [in] bMirrorX_ 是否关于 X 轴镜像
            * @details 按指定的仿射参数对控制点整体进行变换。
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前对象
            * @return 新的几何对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            /**
            * @brief 计算指定区间内的 B 样条基函数值
            * @param [in] i 控制点索引
            * @param [in] p 阶数
            * @param [in] u 参数值
            * @return 对应的基函数值
            */
            double BasisFunc(IN const int& i, IN const int& p, IN const double& u) const;

        private:
            std::vector<Point3> m_lstControlPoints; //!< 控制点列表
            std::vector<double> m_lstKnotVector;    //!< 节点向量
            int m_nDegree;                          //!< 曲线阶数
        };
    }
}
