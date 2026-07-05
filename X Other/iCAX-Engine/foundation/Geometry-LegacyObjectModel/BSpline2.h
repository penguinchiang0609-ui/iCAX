#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"
#include <vector>
#include <memory>

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维 B 样条曲线（不带权重）
        * @details
        * BSpline2 表示二维普通 B 样条曲线，由控制点、节点向量和阶数定义。
        * 曲线定义在局部坐标系中，可通过 Transform() 接口应用二维仿射变换。
        *
        * 数学表示：
        * \f[
        * C(u) = \sum_{i=0}^{n} N_{i,p}(u) P_i
        * \f]
        * 其中 \f$N_{i,p}(u)\f$ 为 B 样条基函数，\f$P_i\f$ 为控制点，\f$p\f$ 为阶数。
        */
        class _GEOMETRY_EXP BSpline2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstControlPoints_ 控制点列表
            * @param [in] lstKnotVector_ 节点向量
            * @param [in] nDegree_ 曲线阶数（p >= 1）
            * @throw std::invalid_argument 如果节点向量长度不满足 n+degree+1
            */
            BSpline2(IN const std::vector<Point2>& lstControlPoints_,
                IN const std::vector<double>& lstKnotVector_,
                IN const int& nDegree_);

            /**
            * @brief 析构函数
            */
            virtual ~BSpline2();

        public: //!< 属性访问
            /**
            * @brief 获取控制点列表
            * @return 控制点数组引用
            */
            const std::vector<Point2>& ControlPoints() const;

            /**
            * @brief 获取曲线阶数
            * @return 曲线阶数
            */
            int Degree() const;

            /**
            * @brief 获取节点向量
            * @return 节点向量数组引用
            */
            const std::vector<double>& KnotVector() const;

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
            double BasisFunc(IN const int& i, IN const int& p, IN const double& u) const;

        private:
            std::vector<Point2> m_lstControlPoints; //!< 控制点列表
            std::vector<double> m_lstKnotVector;    //!< 节点向量
            int m_nDegree;                         //!< 曲线阶数
        };
    }
}
