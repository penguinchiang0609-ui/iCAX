#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维椭圆弧类
        * @details
        * EllipseArc3 表示定义在三维空间某一平面内的开口椭圆弧。
        *
        * 特性说明：
        * - 椭圆弧位于由 dirNormal_ 和 dirRight_ 定义的局部坐标平面内；
        * - 通过长轴半径（a）、短轴半径（b）、起始角、终止角定义；
        * - 可设置顺时针或逆时针方向；
        * - 支持平移、缩放、剪切、反转等仿射变换。
        *
        * 在局部坐标中：
        * \f[
        * P(u) = (a \cos u, b \sin u)
        * \f]
        * 其中参数范围为 [StartAngle, EndAngle]。
        */
        class _GEOMETRY_EXP EllipseArc3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造椭圆弧
            * @param [in] ptCenter_ 椭圆中心点
            * @param [in] nRadiusA_ 长轴半径（沿局部 X 方向）
            * @param [in] nRadiusB_ 短轴半径（沿局部 Y 方向）
            * @param [in] nStartRad_ 起始角（弧度）
            * @param [in] nEndRad_ 终止角（弧度）
            * @param [in] bCCW_ 是否为逆时针方向（默认 true）
            * @param [in] dirNormal_ 椭圆平面的法向量
            * @param [in] dirRight_ 椭圆角度 0 对应方向
            * @note
            * 若 bCCW_ = true，则参数方向为逆时针；
            * 若 bCCW_ = false，则参数方向为顺时针。
            */
            EllipseArc3(IN const Point3& ptCenter_,
                IN const double& nRadiusA_,
                IN const double& nRadiusB_,
                IN const double& nStartRad_,
                IN const double& nEndRad_,
                IN const bool& bCCW_ = true,
                IN const Dir3& dirNormal_ = Dir3::Up(),
                IN const Dir3& dirRight_ = Dir3::Right());

            /**
            * @brief 析构函数
            * @details 默认析构，无需特殊资源释放。
            */
            virtual ~EllipseArc3();

        public: //!< 成员访问接口
            /**
            * @brief 获取长轴半径
            * @return const double& 长轴半径
            */
            const double& MajorRadius() const;

            /**
            * @brief 获取短轴半径
            * @return const double& 短轴半径
            */
            const double& MinorRadius() const;

            /**
            * @brief 获取起始角（弧度）
            * @return const double& 起始角
            */
            const double& StartAngle() const;

            /**
            * @brief 获取终止角（弧度）
            * @return const double& 终止角
            */
            const double& EndAngle() const;

            /**
            * @brief 判断是否为逆时针方向
            * @return true 表示逆时针，false 表示顺时针
            */
            const bool& IsCCW() const;

            /**
            * @brief 设置椭圆弧参数
            * @param [in] nMajorRadius_ 长轴半径
            * @param [in] nMinorRadius_ 短轴半径
            * @param [in] nStartRad_ 起始角（弧度）
            * @param [in] nEndRad_ 终止角（弧度）
            * @param [in] bCCW_ 是否逆时针（默认 false）
            */
            void SetSize(IN const double& nMajorRadius_,
                IN const double& nMinorRadius_,
                IN const double& nStartRad_,
                IN const double& nEndRad_,
                IN const bool& bCCW_);

            /**
            * @brief 将曲线参数映射为极角
            * @param [in] nU_ 参数值（范围 [0,1]）
            * @return 对应的角度（弧度）
            */
            double GetTheta(IN const double& nU_) const;

        public: //!< BndCurve3 接口
            /**
            * @brief 获取曲线朝向类型
            * @param [in] dirNormal_ 法向量
            * @return 曲线朝向类型
            * @details 根据法向与椭圆方向判断顺/逆时针。
            */
            virtual CurveOrientType Orientation(IN const Dir3& dirNormal_) const override;

        public: //!< Curve3 接口
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值
            * @note 对椭圆弧而言，通常为 0。
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值
            * @note 对椭圆弧而言，通常为 1。
            */
            virtual double EndParam() const override;

            /**
            * @brief 计算给定参数对应的点坐标
            * @param [in] nU_ 曲线参数
            * @return 三维点坐标
            * @note 参数范围为 [0,1]，内部映射为角度区间 [StartAngle, EndAngle]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向后，几何形状不变但参数方向取反。
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
            * @brief 对椭圆弧应用缩放与剪切变换
            * @param [in] nScale_ 缩放比例
            * @param [in] nShear_ 剪切参数
            * @param [in] bMirrorX_ 是否沿 X 轴镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前椭圆弧对象
            * @return 新的 Geometry 智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            double m_nMajorRadius;   //!< 长轴半径
            double m_nMinorRadius;   //!< 短轴半径
            double m_nStartRad;      //!< 起始角（弧度）
            double m_nEndRad;        //!< 终止角（弧度）
            bool m_bCCW;             //!< 是否逆时针
        };
    }
}
