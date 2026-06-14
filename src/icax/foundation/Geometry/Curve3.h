#pragma once
#include "GeometryPrimative.h"
#include "Geometry3.h"
#include "../Math/Point3.h"
#include "../Math/Vector3.h"
#include <utility>   // for std::pair
#include <optional>
#include "../Math/Plane3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 三维曲线的边界类型
        * @details
        * 用于描述曲线在参数方向上的有界性：
        * - 无界曲线（kNoneBnd3）：参数区间为 (-∞, +∞)，如无限直线；
        * - 半有界曲线（kHalfBnd3）：参数区间为 [0, +∞)，如射线；
        * - 有界曲线（kBnd3）：参数区间为有限区间 [uStart, uEnd]，如线段、圆弧、有限样条。
        */
        enum _GEOMETRY_EXP CurveBndType3
        {
            kNoneBnd3 = 0,  //!< 无界曲线
            kHalfBnd3,      //!< 半有界曲线（一个方向有限，一个方向无限）
            kBnd3           //!< 有界曲线（参数范围有限）
        };

        /**
        * @brief 三维参数曲线抽象基类
        * @details
        * Curve3 表示所有三维参数化曲线（如直线、圆、圆弧、样条等）的统一接口。
        * 每条曲线定义了一个参数域 u，通过 Evaluate(u) 可以计算曲线上的点。
        * 曲线可以是有界的（如圆弧）或无界的（如直线）。
        */
        class _GEOMETRY_EXP Curve3 : public Geometry3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 曲线的局部坐标系
            */
            explicit Curve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~Curve3();

        public: //!< 基础曲线接口
            /**
            * @brief 获取曲线边界类型
            * @return 曲线边界类型
            * @see CurveBndType3
            */
            virtual CurveBndType3 BndType() const = 0;

            /**
            * @brief 获取曲线起始参数
            * @return 起始参数值 uStart
            * @note 参数区间定义由具体曲线类型决定。
            */
            virtual double StartParam() const = 0;

            /**
            * @brief 获取曲线终止参数
            * @return 终止参数值 uEnd
            * @note 通常满足 uEnd > uStart。
            */
            virtual double EndParam() const = 0;

            /**
            * @brief 参数归一化
            * @param [in] nU_ 原始参数值
            * @return 归一化到标准参数范围内的值
            * @note 具体实现由子类定义，例如在周期曲线中进行模运算。
            */
            virtual double NormalizeParam(IN const double& nU_) const = 0;

            /**
            * @brief 在给定参数处计算三维点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的三维点坐标
            */
            virtual Point3 Evaluate(IN const double& nU_) const = 0;

            /**
            * @brief 反转曲线方向
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            */
            virtual void Reverse() = 0;

            /**
            * @brief 是否为周期曲线
            * @return 若曲线在几何上封闭且参数连续，返回 true
            */
            virtual bool IsPeriodic() const = 0;

            /**
            * @brief 返回曲线周期
            * @return 若为周期曲线，返回其周期；否则返回 0.0
            */
            virtual double Period() const = 0;

            /*
            * @brief 获取建议平面
            * @return std::optional<Plane3>
            */
            virtual std::optional<Plane3> SuggestPlane() const = 0;

            /*
            * @brief 判断是否在指定平面内
            * @param [in] pln_
            * @return bool
            */
            virtual bool IsOnPlane(IN const Plane3& pln_) const = 0;
        };
    }
}
