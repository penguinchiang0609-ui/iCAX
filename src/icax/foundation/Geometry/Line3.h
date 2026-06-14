#pragma once
#include "GeometryPrimative.h"
#include "UnBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维无限直线
        * @details
        * Line3 表示二维空间中的无界直线，属于 UnBndCurve2（无界曲线）体系。
        *
        * 曲线参数 u 对应直线公式：
        *   C(u) = csLocal().Location() + u * csLocal().XDiretion()
        *
        * @note
        * 1. 方向由局部坐标系 X 轴决定，原点为直线参考点。
        * 2. 无需单独存储起点和方向向量。
        * 3. 曲线参数区间为 (-∞, +∞)。
        * 4. Tangent/D1 等导数信息不在此类中提供，由算法层处理。
        */
        class _GEOMETRY_EXP Line3 final : public UnBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系，X 轴表示直线方向，原点在直线上
            * @details
            * 构造一个二维无限直线，局部坐标系提供方向和参考点。
            */
            Line3(IN const Point3& pt0_, IN const Point3& pt1_);

            /**
            * @brief 析构函数
            * @details 默认析构，无需额外操作
            */
            virtual ~Line3();

        public: //!< UnBndCurve2 / Curve2 接口实现

            /**
            * @brief 根据参数计算直线上点坐标
            * @param [in] nU_ 曲线参数 u
            * @return 对应二维点坐标
            * @details 参数 u 对应公式：
            *   C(u) = csLocal().Origin() + u * csLocal().XAxis()
            */
            virtual Point3 Evaluate(const double& nU_) const override final;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

            /**
            * @brief 获取曲线所在平面
            * @return 若共面，返回对应的 Plane3；否则返回 std::nullopt
            * @details 对于直线段，此平面可取局部坐标系的 XY 平面。
            */
            virtual std::optional<Plane3> SuggestPlane() const override;

            /*
            * @brief 判断是否在指定平面内
            * @param [in] pln_
            * @return bool
            */
            virtual bool IsOnPlane(IN const Plane3& pln_) const override;

        public: //!< Geometry2 接口实现
            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_
            */
            virtual void ScaleAndShear(IN const Double3& nScale_, IN const Double6& nShear_, IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆
            * @return 新的对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;
        };
    }
}
