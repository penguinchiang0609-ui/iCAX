#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维渐开线（Clothoid / Euler Spiral / Cornu Spiral）
        * @details
        * 渐开线是一种曲率随弧长线性变化的空间曲线，常用于道路、轨道、
        * 机器人路径以及 CAD/CAM 场景中的平滑过渡段建模。
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
        * x(s) = \int_0^s \cos(\theta(u))\,du,\quad
        * y(s) = \int_0^s \sin(\theta(u))\,du
        * \f]
        *
        * 在三维场景中，通过局部坐标系（m_csLocal）定义其空间位置与方向。
        */
        class _GEOMETRY_EXP Clothoid3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系（定义起点与起始方向）
            * @param [in] nKappa0_ 起始曲率
            * @param [in] nKappaRate_ 曲率变化率（dκ/ds）
            * @param [in] nLength_ 曲线总长度
            * @param [in] nTheta0_ 起始切向角（弧度）
            */
            Clothoid3(IN const CoordSys3& csLocal_,
                IN const double& nKappa0_,
                IN const double& nKappaRate_,
                IN const double& nLength_,
                IN const double& nTheta0_);

            /**
            * @brief 默认析构函数
            */
            virtual ~Clothoid3();

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
            * @return 曲率变化率 \f$a = d\kappa/ds\f$
            */
            const double& CurvatureRate() const;

            /**
            * @brief 设置曲率变化率
            * @param [in] nKappaRate_ 曲率变化率
            */
            void SetCurvatureRate(IN const double& nKappaRate_);

            /**
            * @brief 获取曲线长度
            * @return 曲线总长度
            */
            const double& Length() const;

            /**
            * @brief 设置曲线长度
            * @param [in] nLength_ 曲线长度
            */
            void SetLength(IN const double& nLength_);

            /**
            * @brief 获取起始切向角
            * @return 起始切向角（弧度）
            */
            const double& Theta0() const;

            /**
            * @brief 设置起始切向角
            * @param [in] nTheta_ 起始切向角（弧度）
            */
            void SetTheta0(IN const double& nTheta_);

        public: //!< Curve3 接口实现
            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间通常为 [0, Length()]
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            */
            virtual double EndParam() const override;

            /**
            * @brief 在给定参数处计算曲线点坐标
            * @param [in] nU_ 曲线参数（弧长）
            * @return 对应的三维点坐标
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向反转。
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
            * @param [in] bMirrorX_ 是否沿 X 轴镜像
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现
            /**
            * @brief 克隆当前对象
            * @return 新的 Clothoid3 智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            CoordSys3 m_csLocal;   //!< 局部坐标系（定义起点与初始方向）
            double m_nKappa0;      //!< 起始曲率
            double m_nKappaRate;   //!< 曲率变化率
            double m_nLength;      //!< 曲线长度
            double m_nTheta0;      //!< 起始切向角（弧度）
        };
    }
}
