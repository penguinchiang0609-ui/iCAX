#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维圆弧
        * @details
        * Arc2 表示定义在局部坐标系内的二维开口圆弧。
        *
        * - 圆弧以局部坐标系原点为圆心；
        * - 通过半径、起始角、终止角定义；
        * - 可设置顺时针或逆时针方向；
        * - 可通过 Transform() 接口应用仿射变换；
        *
        * 在局部坐标中：
        * \f[
        * P(u) = (r \cos u, r \sin u)
        * \f]
        * 其中参数范围为 [0, 1.0]。
        * 此处选择采用归一化的参数区间是为了屏蔽顺逆时针对于上层调用者的影响。
        */
        class _GEOMETRY_EXP Arc2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] ptCenter_ 圆心坐标
            * @param [in] nRadius_ 圆弧半径
            * @param [in] nStartRad_ 起始角（弧度）
            * @param [in] nEndRad_ 终止角（弧度）
            * @param [in] bCCW_ 是否为逆时针方向（默认 true）
            * @param [in] dirRight_ 圆心角为零的方向
            * @note
            * 若 bCCW_ = true，则参数方向为逆时针；
            * 若 bCCW_ = false，则参数方向为顺时针。
            *
            * 圆心固定在局部坐标原点。
            */
            Arc2(IN const Point2& ptCenter_, IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_ = true, IN const Dir2& dirRight_ = Dir2::Right());

            /**
            * @brief 默认析构函数
            */
            virtual ~Arc2();

        public: //!< 基础属性访问接口
            /**
            * @brief 半径
            * @return double&
            */
            const double& Radius() const;

            /**
            * @brief 起始角（弧度）
            * @return const double&
            */
            const double& StartAngle() const;

            /**
            * @brief 结束角（弧度）
            * @return const double&
            */
            const double& EndAngle() const;

            /**
            * @brief 是否为逆时针方向
            * @return true 表示逆时针，false 表示顺时针
            */
            const bool& IsCCW() const;

            /**
            * @brief 是否为逆时针方向
            * @return true 表示逆时针，false 表示顺时针
            */
            bool& IsCCW();

            /*
            * @brief 设置尺寸
            * @param [in] nRadius_ 圆弧半径
            * @param [in] nStartRad_ 起始角（弧度）
            * @param [in] nEndRad_ 终止角（弧度）
            * @param [in] bCCW_ 是否为逆时针方向（默认 false）
            * @remark 此处提供bCCW_而不是借助reverse的原因是顺逆时针是圆弧的固有属性，等价于优劣弧，与方向反转并不等价，务必注意
            */
            void SetSize(IN const double& nRadius_, IN const double& nStartRad_, IN const double& nEndRad_, IN const bool& bCCW_);

            /*
            * @brief 根据参数得到角度
            * @param [in] nU_
            * @return double
            */
            double GetTheta(IN const double& nU_) const;

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
            /*
            * @brief 获取曲线朝向类型
            * @return CurveOrientType
            */
            virtual CurveOrientType Orientation() const override;

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
            double m_nRadius;     //!< 圆弧半径
            double m_nStartRad;   //!< 起始角（弧度）
            double m_nEndRad;     //!< 终止角（弧度）
            bool m_bCCW;          //!< 是否逆时针
        };
    }
}
