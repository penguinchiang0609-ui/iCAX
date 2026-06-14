#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 二维渐开线（Clothoid / Euler Spiral / Cornu Spiral）
        * @details
        * 渐开线是一种曲率随弧长线性变化的曲线，用于道路、轨道、机器人路径等场景，
        * 实现从直线到圆弧的平滑过渡。
        *
        * 数学定义：
        * \f[
        * \kappa(s) = \kappa_0 + a \cdot s
        * \f]
        * 其中 \f$\kappa_0\f$ 为起始曲率，\f$a\f$ 为曲率变化率。
        *
        * 参数方程：
        * \f[
        * \theta(s) = \kappa_0 s + \frac{1}{2} a s^2
        * \f]
        * \f[
        * x(s) = \int_0^s \cos(\theta(u)) du,\quad
        * y(s) = \int_0^s \sin(\theta(u)) du
        * \f]
        */
        class _GEOMETRY_EXP Clothoid2 final : public SglBndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] nKappa0_ 起始曲率
            * @param [in] nKappaRate_ 曲率变化率 (dk/ds)
            * @param [in] nLength_ 曲线长度
            * @param [in] nTheta0_ 起始切向角（弧度）
            */
            Clothoid2(IN const CoordSys2& csLocal_, IN const double& nKappa0_, IN const double& nKappaRate_, IN const double& nLength_, IN const double& nTheta0_);

            /**
            * @brief 默认析构函数
            */
            virtual ~Clothoid2();

        public: //!< 属性访问
            /**
            * @brief 获取起始曲率
            * @return 起始曲率 \f$\kappa_0\f$
            */
            const double& StartCurvature() const;

            /**
            * @brief 设置起始曲率
            * @param [in] nKappa0_ 起始曲率
            */
            void SetStartCurvature(IN const double& nKappa0_);

            /**
            * @brief 获取曲率变化率
            * @return 曲率变化率 a = dκ/ds
            */
            const double& CurvatureRate() const;

            /**
            * @brief 设置曲率变化率
            * @param [in] nKappaRate_ 曲率变化率
            */
            void SetCurvatureRate(IN const double& nKappaRate_);

            /**
            * @brief 获取曲线长度
            * @return 曲线长度
            */
            const double& Length() const;

            /**
            * @brief 设置曲线长度
            * @param [in] nLength_ 曲线长度
            */
            void SetLength(IN const double& nLength_);

            /*
            * @brief 获取Theta0
            * @return const double& 
            */
            const double& Theta0() const;

            /*
            * @brief 设置Theta0
            * @param [in] nTheta_
            */
            void SetTheta0(IN const double& nTheta_);

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
            CoordSys2 m_csLocal;   //!< 局部坐标系（起点位置 + 起始方向）
            double m_nKappa0;      //!< 起始曲率
            double m_nKappaRate;   //!< 曲率变化率
            double m_nLength;      //!< 曲线长度
            double m_nTheta0;      //!< 起始切向角（弧度)
        };
    }
}
