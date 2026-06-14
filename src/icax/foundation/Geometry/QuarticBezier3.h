#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @class QuarticBezier3
        * @brief 三维四阶贝塞尔曲线
        * @ingroup Geometry
        * @details
        * QuarticBezier3 表示定义在三维空间内的四阶（即五控制点）贝塞尔曲线。
        *
        * 曲线由 5 个控制点定义：
        * - P0：起点；
        * - P1、P2、P3：中间控制点；
        * - P4：终点。
        *
        * 数学定义：
        * \f[
        *   B(u) = (1 - u)^4 P_0 + 4(1 - u)^3 u P_1 + 6(1 - u)^2 u^2 P_2 + 4(1 - u)u^3 P_3 + u^4 P_4, \quad u \in [0,1]
        * \f]
        *
        * 特性：
        * - 支持通过 Evaluate() 计算任意参数点；
        * - 支持几何反向（Reverse）；
        * - 可执行三维仿射变换（Transform）；
        * - 可克隆（Clone）；
        * - 通常用于表示复杂平滑的自由曲线段。
        */
        class _GEOMETRY_EXP QuarticBezier3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] pt0_ 起点 P0
            * @param [in] pt1_ 控制点 P1
            * @param [in] pt2_ 控制点 P2
            * @param [in] pt3_ 控制点 P3
            * @param [in] pt4_ 终点 P4
            * @note 构造后参数定义在 [0, 1] 区间。
            */
            QuarticBezier3(IN const Point3& pt0_, IN const Point3& pt1_, IN const Point3& pt2_, IN const Point3& pt3_, IN const Point3& pt4_);

            /**
            * @brief 默认析构函数
            */
            virtual ~QuarticBezier3();

        public: //!< 控制点访问与修改

            /**
            * @brief 获取起点 P0
            * @return 起点的引用
            */
            const Point3& P0() const;

            /**
            * @brief 获取控制点 P1
            * @return 控制点1的引用
            */
            const Point3& P1() const;

            /**
            * @brief 获取控制点 P2
            * @return 控制点2的引用
            */
            const Point3& P2() const;

            /**
            * @brief 获取控制点 P3
            * @return 控制点3的引用
            */
            const Point3& P3() const;

            /**
            * @brief 获取终点 P4
            * @return 终点的引用
            */
            const Point3& P4() const;

            /**
            * @brief 设置起点 P0
            * @param [in] pt_ 新的起点坐标
            */
            void SetP0(IN const Point3& pt_);

            /**
            * @brief 设置控制点 P1
            * @param [in] pt_ 新的控制点1坐标
            */
            void SetP1(IN const Point3& pt_);

            /**
            * @brief 设置控制点 P2
            * @param [in] pt_ 新的控制点2坐标
            */
            void SetP2(IN const Point3& pt_);

            /**
            * @brief 设置控制点 P3
            * @param [in] pt_ 新的控制点3坐标
            */
            void SetP3(IN const Point3& pt_);

            /**
            * @brief 设置终点 P4
            * @param [in] pt_ 新的终点坐标
            */
            void SetP4(IN const Point3& pt_);

        public://!< BndCurve3 接口

        public://!< Curve3 接口
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

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            * @note 参数范围由具体曲线类型定义，例如 [0,1] 或 [0,2π]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
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
            * @brief 执行缩放与剪切操作
            * @param [in] nScale_ 缩放向量 (x,y,z)
            * @param [in] nShear_ 剪切参数 (6维)
            * @param [in] bMirrorXoZ_ 是否沿 X/Z 轴镜像
            * @details
            * 对圆心及圆弧平面进行相应仿射变换。
            */
            virtual void ScaleAndShear(
                IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorXoZ_) override;

        public: //!< Geometry 接口实现

            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            Point3 m_pt0; //!< 起点 P0
            Point3 m_pt1; //!< 控制点 P1
            Point3 m_pt2; //!< 控制点 P2
            Point3 m_pt3; //!< 控制点 P3
            Point3 m_pt4; //!< 终点 P4
        };
    }
}
