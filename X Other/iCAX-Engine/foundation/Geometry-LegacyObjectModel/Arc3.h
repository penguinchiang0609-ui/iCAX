#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维圆弧
        * @details
        * Arc3 表示定义在局部坐标系内的一段三维开口圆弧。
        *
        * - 圆弧由半径、起始角、终止角和方向（顺/逆时针）定义；
        * - 圆弧所在平面由法向 `dirNormal_` 与起始方向 `dirRight_` 确定；
        * - 支持仿射变换（Transform、ScaleAndShear）；
        * - 支持方向反转（Reverse）；
        *
        * 参数化采用归一化区间 u ∈ [0,1]，与真实角度线性映射：
        * \f[
        *   P(u) = C + R \cdot (\cos\theta(u) \cdot X + \sin\theta(u) \cdot Y)
        * \f]
        * 其中 \f$\theta(u) = \theta_{start} + u \cdot (\theta_{end} - \theta_{start})\f$。
        */
        class _GEOMETRY_EXP Arc3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_   圆心坐标（局部）
            * @param [in] nRadius_    圆弧半径
            * @param [in] nStartRad_  起始角（弧度）
            * @param [in] nEndRad_    终止角（弧度）
            * @param [in] bCCW_       是否为逆时针方向（默认 true）
            * @param [in] dirNormal_  圆弧平面法向
            * @param [in] dirRight_   起始角为零时的方向向量
            * @note
            * 若 bCCW_ = true，则参数方向为逆时针；
            * 若 bCCW_ = false，则参数方向为顺时针。
            */
            Arc3(IN const Point3& ptCenter_,
                IN const double& nRadius_,
                IN const double& nStartRad_,
                IN const double& nEndRad_,
                IN const bool& bCCW_ = true,
                IN const Dir3& dirNormal_ = Dir3::Up(),
                IN const Dir3& dirRight_ = Dir3::Right());

            /**
            * @brief 析构函数
            */
            virtual ~Arc3();

        public: //!< 基础属性访问接口

            /**
            * @brief 获取圆弧半径
            * @return 圆弧半径
            */
            const double& Radius() const;

            /**
            * @brief 获取起始角（弧度）
            * @return 起始角
            */
            const double& StartAngle() const;

            /**
            * @brief 获取终止角（弧度）
            * @return 终止角
            */
            const double& EndAngle() const;

            /**
            * @brief 获取是否逆时针方向
            * @return true 表示逆时针，false 表示顺时针
            */
            const bool& IsCCW() const;

            /**
            * @brief 获取/修改是否逆时针方向
            * @return true 表示逆时针，false 表示顺时针
            */
            bool& IsCCW();

            /**
            * @brief 设置圆弧参数
            * @param [in] nRadius_    半径
            * @param [in] nStartRad_  起始角（弧度）
            * @param [in] nEndRad_    终止角（弧度）
            * @param [in] bCCW_       是否逆时针（默认 false）
            * @remark
            * bCCW_ 表示圆弧的固有方向，与 Reverse() 的参数反转不同。
            * 顺逆时针等价于优劣弧属性，而非参数方向。
            */
            void SetSize(IN const double& nRadius_,
                IN const double& nStartRad_,
                IN const double& nEndRad_,
                IN const bool& bCCW_);

            /**
            * @brief 根据参数计算角度
            * @param [in] nU_ 曲线参数（范围 [0,1]）
            * @return 当前角度（弧度）
            */
            double GetTheta(IN const double& nU_) const;

        public: //!< BndCurve3 接口
            /**
            * @brief 获取曲线朝向类型
            * @param [in] dirNormal_ 比较用的参考法向
            * @return 曲线方向类型（顺时针/逆时针/未知）
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const override;

        public: //!< Curve3 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值（固定为 0.0）
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值（固定为 1.0）
            */
            virtual double EndParam() const override;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数（范围 [0,1]）
            * @return 对应的三维点坐标（世界坐标系）
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 交换起始与终止角，使参数方向相反。
            * 不改变 bCCW_ 属性（顺逆时针方向）。
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
            * @brief 克隆当前几何对象
            * @return 新的 Arc3 智能指针副本
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            double m_nRadius;    //!< 圆弧半径
            double m_nStartRad;  //!< 起始角（弧度）
            double m_nEndRad;    //!< 终止角（弧度）
            bool   m_bCCW;       //!< 是否为逆时针方向
        };
    }
}
