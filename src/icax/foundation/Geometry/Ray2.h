#pragma once
#include "GeometryPrimative.h"
#include "HalfBndCurve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维射线（半无限直线）
        * @details
        * Ray2 表示二维空间中的半无限直线，继承自 HalfBndCurve2。
        * 曲线从局部坐标系原点沿 X 轴方向无限延伸。
        *
        * 曲线参数 u 对应公式：
        *   C(u) = csLocal().Origin() + u * csLocal().XAxis()
        *
        * @note
        * 1. 射线方向由局部坐标系 X 轴决定，原点为参考起点。
        * 2. 无需单独存储起点或方向向量。
        * 3. 曲线参数区间为 [0, +∞)。
        * 4. Tangent / D1 / D2 等导数信息由算法层提供。
        */
        class _GEOMETRY_EXP Ray2 final : public HalfBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系，X 轴表示射线方向，原点在射线上
            * @details
            * 构造一个二维射线，局部坐标系提供方向和参考起点。
            */
            explicit Ray2(IN const Point2& ptStart_, IN const Point2& ptTarget_);

            /**
            * @brief 析构函数
            * @details 默认析构，无需额外操作
            */
            virtual ~Ray2();

        public: //!< Curve2 / HalfBndCurve2 接口实现

            /**
            * @brief 根据参数计算射线上点坐标
            * @param [in] nU_ 曲线参数 u
            * @return 对应二维点坐标
            * @details 参数 u 对应公式：
            *   C(u) = csLocal().Origin() + u * csLocal().XAxis()
            */
            virtual Point2 Evaluate(const double& nU_) const override final;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() override;

        public: //!< Geometry2 接口实现

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
            * @brief 克隆对象
            * @return 返回一个新的 Ray2 智能指针，内容与当前对象一致
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            bool m_bReversed;
        };
    }
}
