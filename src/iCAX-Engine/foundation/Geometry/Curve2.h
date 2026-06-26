#pragma once
#include "GeometryPrimative.h"
#include "Geometry2.h"
#include "../Math/Point2.h"
#include "../Math/Vector2.h"
#include <utility>   // for std::pair

namespace iCAX
{
    namespace Geom
    {
        /*
        * @brief 二维曲线的边界类型
        * @details
        * 用于描述曲线的参数化边界性质：
        * - 无界曲线（kNoneBnd）：曲线在参数方向上无限延伸，例如直线、射线。
        * - 半有界曲线（kHalfBnd2）：曲线在一个方向上有界，在另一个方向上无界，例如射线。
        * - 有界曲线（kBnd2）：曲线在参数方向上有界，例如线段、圆弧、有限 B-spline。
        */
        enum _GEOMETRY_EXP CurveBndType2
        {
            kNoneBnd2 = 0,    //!< 无界曲线
            kHalfBnd2,       //!< 半有界曲线（一个方向有限，一个方向无限）
            kBnd2             //!< 有界曲线（参数范围有限）
        };

        /**
        * @brief 二维参数曲线抽象基类
        * @details
        * Curve2 表示所有二维参数化曲线（如直线、圆、圆弧、样条等）的统一接口。
        * 每条曲线定义了一个参数域 (u)，通过 Evaluate(u) 可以获取点的位置，
        * 通过 Tangent(u) 获取切向方向。曲线可以是有界的（如圆弧）或无界的（如直线）。
        */
        class _GEOMETRY_EXP Curve2 : public Geometry2
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] csLocal_ 自身坐标系
            */
            Curve2(IN const CoordSys2& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~Curve2();

        public: //!< 基础曲线接口
            /**
            * @brief 判断曲线是否为有界曲线
            * @details
            * 有界曲线具有有限的参数范围，例如线段、圆弧、样条段；
            * 无界曲线（如直线、射线）则参数区间为无穷。
            * @return 若曲线有界返回 true，否则返回 false。
            */
            virtual CurveBndType2 BndType() const = 0;

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

            /*
            * @brief 参数归一化
            * @param [in] nU_double
            * @return
            */
            virtual double NormalizeParam(IN const double& nU_) const = 0;

            /**
            * @brief 在给定参数处计算点坐标
            * @param [in] nU_ 曲线参数
            * @return 对应的二维点坐标
            * @note 参数范围由具体曲线类型定义，例如 [0,1] 或 [0,2π]。
            */
            virtual Point2 Evaluate(IN const double& nU_) const = 0;

            /**
            * @brief 反转
            * @details
            * 反向曲线在几何上与原曲线重合，但参数方向相反，
            * 即 u 的增大方向与原曲线相反。
            * @return 指向反向曲线的新智能指针。
            */
            virtual void Reverse() = 0;

            /*
            * @brief 是否周期性
            * @return bool
            */
            virtual bool IsPeriodic() const = 0;

            /*
            * @brief 周期
            * @return bool
            */
            virtual double Period() const = 0;
        };
    }
}
