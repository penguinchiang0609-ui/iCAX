#pragma once
#include "GeometryPrimative.h"
#include "SglBndCurve3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维 Catmull-Rom 样条曲线
        * @details
        * CatmullRomSpline3 表示一种平滑插值曲线，曲线严格通过所有控制点，
        * 常用于路径插值、相机轨迹、刀具轨迹平滑等场景。
        *
        * 曲线由分段三次样条构成，每一段由 4 个相邻控制点定义：
        * \f[
        * P_i(t) = 0.5 \cdot \left[
        * (2P_i) + (-P_{i-1} + P_{i+1})t +
        * (2P_{i-1} - 5P_i + 4P_{i+1} - P_{i+2})t^2 +
        * (-P_{i-1} + 3P_i - 3P_{i+1} + P_{i+2})t^3
        * \right]
        * \f]
        * 其中参数 \f$t \in [0,1]\f$，曲线在每个区间内连续且可微。
        *
        * @note
        * - 当 tension = 0 时为标准 Catmull-Rom 样条；
        * - 当 tension = 1 时退化为折线；
        * - tension < 0 时曲线更平滑，>0 时更趋紧。
        */
        class _GEOMETRY_EXP CatmullRomSpline3 final : public SglBndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] lstCtrlPts_ 控制点序列（至少 4 个）
            * @param [in] nTension_ 张力系数（默认 0.0）
            * @throw std::invalid_argument 若控制点数少于 4 个
            */
            CatmullRomSpline3(IN const std::vector<Point3>& lstCtrlPts_,
                IN const double& nTension_ = 0.0);

            /**
            * @brief 析构函数
            */
            virtual ~CatmullRomSpline3();

        public: //!< 属性访问

            /**
            * @brief 获取控制点序列
            * @return 控制点数组常量引用
            */
            const std::vector<Point3>& ControlPoints() const;

            /**
            * @brief 获取张力系数
            * @return 当前 tension 值
            */
            const double& Tension() const;

            /**
            * @brief 设置张力系数
            * @param [in] nTension_ 新的 tension 值
            */
            void SetTension(IN const double& nTension_);

            /**
            * @brief 设置控制点序列
            * @param [in] lstCtrlPts_ 控制点列表
            * @throw std::invalid_argument 若控制点数少于 4 个
            */
            void SetCtrlPoints(IN const std::vector<Point3>& lstCtrlPts_);

        public: //!< Curve3 接口实现

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart（通常为 0.0）
            */
            virtual double StartParam() const override;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd（通常为控制段数）
            */
            virtual double EndParam() const override;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的三维点坐标
            * @note 参数范围通常为 [StartParam(), EndParam()]。
            */
            virtual Point3 Evaluate(IN const double& nU_) const override;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向后的曲线在空间上与原曲线重合，但参数方向相反，
            * 即 u 的递增方向与原定义方向相反。
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
            * @param [in] nScale_ 缩放系数向量
            * @param [in] nShear_ 剪切参数集合
            * @param [in] bMirrorX_ 是否进行 X 轴镜像
            * @details 对所有控制点应用指定仿射变换。
            */
            virtual void ScaleAndShear(IN const Double3& nScale_,
                IN const Double6& nShear_,
                IN const bool& bMirrorX_) override;

        public: //!< Geometry 接口实现

            /**
            * @brief 克隆当前对象
            * @return 新的几何对象智能指针
            */
            virtual std::shared_ptr<Geometry> Clone() const override;

        private:
            std::vector<Point3> m_lstCtrlPts; //!< 控制点序列
            double m_nTension;                //!< 张力系数
        };
    }
}
