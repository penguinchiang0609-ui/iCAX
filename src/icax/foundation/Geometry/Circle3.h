#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维完整圆曲线
        * @details
        * Circle3 表示三维空间中位于某一平面上的完整圆（闭合曲线）。
        * - 参数区间固定为 [0, 1]，u=0 与 u=1 对应同一点。
        * - 曲线方向由法向量 dirNormal_ 和方向向量 dirRight_ 共同决定。
        * - 可通过 Transform() 或 ScaleAndShear() 实现几何变换。
        * - 通常用于封闭轮廓（如 Loop3 或 Region3）的构成。
        */
        class _GEOMETRY_EXP Circle3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 圆心坐标
            * @param [in] nRadius_ 圆半径
            * @param [in] bCCW_ 是否为逆时针方向（相对法向 dirNormal_）
            * @param [in] dirNormal_ 圆所在平面的法向量
            * @param [in] dirRight_ 参数 u=0 时的初始方向向量
            */
            Circle3(IN const Point3& ptCenter_,
                IN const double& nRadius_,
                IN const bool& bCCW_ = true,
                IN const Dir3& dirNormal_ = Dir3::Up(),
                IN const Dir3& dirRight_ = Dir3::Right());

            /**
            * @brief 析构函数
            * @details 默认析构，无需特殊资源释放。
            */
            virtual ~Circle3();

        public: //!< 属性访问接口
            /**
            * @brief 获取圆半径
            * @return const double& 半径值
            */
            const double& Radius() const;

            /**
            * @brief 设置圆半径
            * @param [in] nRadius_ 新的半径值
            */
            void SetSize(IN const double& nRadius_);

            /**
            * @brief 根据参数计算对应圆心角
            * @param [in] nU_ 参数值（范围 [0,1]）
            * @return 对应的圆心角（单位：弧度）
            */
            double GetTheta(IN const double& nU_) const;

        public: //!< BndCurve3 接口实现
            /**
            * @brief 获取曲线的朝向类型
            * @param [in] dirNormal_ 参考法向
            * @return 曲线朝向类型
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const override;

        public: //!< Curve3 接口实现
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数（恒为 0.0）
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数（恒为 1.0）
            */
            virtual double EndParam() const override;

            /**
            * @brief 计算给定参数对应的点坐标
            * @param [in] nU_ 曲线参数（范围 [0,1]）
            * @return 对应的三维点坐标
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反转后曲线几何形状保持不变，但参数方向相反，
            * 即 u 增大方向与原曲线相反。
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
            * @brief 对圆进行缩放与剪切
            * @param [in] nScale_ 缩放因子（X、Y、Z 方向）
            * @param [in] nShear_ 剪切参数
            * @param [in] bMirrorXoZ_ 是否沿 X 或 Z 方向镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorXoZ_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前对象
            * @return 新的 Circle3 智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            double m_nRadius;   //!< 圆半径
            bool m_bCCW;        //!< 逆时针方向标志
        };
    }
}
