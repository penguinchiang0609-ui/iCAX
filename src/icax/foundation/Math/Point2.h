#pragma once

#include "Math.h"
#include "../Data/Array1.h"
#include "Vector2.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 二维坐标点类
        * @details
        *   - 表示平面直角坐标系中的一个点
        *   - 内部使用 Double2 存储坐标 (x, y)
        *   - 可与 Vector2 配合实现几何运算，例如点加向量、点间距离等
        */
        class _MATH_EXP Point2 final
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 将点初始化为原点 (0,0)
            */
            Point2();

            /**
            * @brief 使用 Double2 初始化点
            * @param [in] nXY_ 二维坐标 (x, y)
            */
            Point2(IN const Double2& nXY_);

            /**
            * @brief 使用两个浮点数初始化点
            * @param [in] nX_ X 坐标
            * @param [in] nY_ Y 坐标
            */
            Point2(IN const double& nX_, IN const double& nY_);

            /**
            * @brief 析构函数
            */
            ~Point2();

        public: //!< 成员访问接口
            /**
            * @brief 获取/设置 X 坐标
            * @return double& 可修改的 X 坐标引用
            */
            double& X();

            /**
            * @brief 获取 X 坐标（只读）
            * @return const double& X 坐标引用
            */
            const double& X() const;

            /**
            * @brief 获取/设置 Y 坐标
            * @return double& 可修改的 Y 坐标引用
            */
            double& Y();

            /**
            * @brief 获取 Y 坐标（只读）
            * @return const double& Y 坐标引用
            */
            const double& Y() const;

            /**
            * @brief 获取/设置 XY 坐标对
            * @return Double2& 可修改的坐标对引用
            */
            Double2& XY();

            /**
            * @brief 获取 XY 坐标对（只读）
            * @return const Double2& 坐标对引用
            */
            const Double2& XY() const;

            /*
            * @brief 判断是否为非法点
            * @return bool
            */
            bool IsNil() const;

        public: //!< 几何运算方法
            /**
            * @brief 点的线性插值
            * @param [in] ptOther_ 目标点
            * @param [in] nFactor_ 插值因子 [0,1]
            * @return Point2 插值得到的新点
            */
            Point2 Lerp(IN const Point2& ptOther_, IN const double& nFactor_) const;

            /**
            * @brief 两个点的加权组合
            * @param [in] ptOther_ 另一个点
            * @param [in] nW1_ 当前点的权重
            * @param [in] nW2_ 另一个点的权重
            * @return Point2 加权后的点
            */
            Point2 Combine(IN const Point2& ptOther_, IN const double& nW1_, IN const double& nW2_) const;

            /**
            * @brief 计算点与另一个点之间的平方距离
            * @param [in] ptOther_ 目标点
            * @return double 平方距离
            */
            double DistanceSquared(IN const Point2& ptOther_) const;

            /**
            * @brief 计算点与另一个点之间的距离
            * @param [in] ptOther_ 目标点
            * @return double 距离值
            */
            double Distance(IN const Point2& ptOther_) const;

            /**
            * @brief 判断两个点是否相等（允许误差）
            * @param [in] ptOther_ 目标点
            * @param [in] nTol_ 容差值，默认 EPSILON
            * @return bool 两点是否相等
            */
            bool IsEqual(IN const Point2& ptOther_, IN const double& nTol_ = EPSILON) const;

        public: //!< 常用静态点
            /**
            * @brief 获取原点 (0,0)
            * @return const Point2& 原点
            */
            static const Point2& Zero();

            /**
            * @brief 获取坐标 (1,1)
            * @return const Point2& 点 (1,1)
            */
            static const Point2& One();

            /*
            * @brief 获取非法点(NaN, NaN)
            * @return const Point2&
            */
            static const Point2& Nil();

        private:
            Double2 m_nXY; ///< 内部存储的坐标
        };

        //------------算术运算符-------------
        /**
        * @brief 将向量偏移应用到点上，返回新点
        * @param [in] ptLHS_ 原始点
        * @param [in] vOffset_ 偏移向量
        * @return Point2 偏移后的新点
        */
        _MATH_EXP Point2 operator+(IN const Point2& ptLHS_, IN const Vector2& vOffset_);

        /**
        * @brief 将向量偏移从点中减去，返回新点
        * @param [in] ptLHS_ 原始点
        * @param [in] vOffset_ 减去的向量
        * @return Point2 减去向量后的新点
        */
        _MATH_EXP Point2 operator-(IN const Point2& ptLHS_, IN const Vector2& vOffset_);

        /**
        * @brief 两点相减得到连接向量
        * @param [in] ptLHS_ 被减点
        * @param [in] ptRHS_ 减去点
        * @return Vector2 从 ptRHS_ 指向 ptLHS_ 的向量
        */
        _MATH_EXP Vector2 operator-(IN const Point2& ptLHS_, IN const Point2& ptRHS_);

        /**
        * @brief 将向量偏移应用到点上，并修改原点
        * @param [in,out] ptLHS_ 原点，将被修改
        * @param [in] vOffset_ 偏移向量
        * @return Point2& 修改后的点引用
        */
        _MATH_EXP Point2& operator+=(IN OUT Point2& ptLHS_, IN const Vector2& vOffset_);

        /**
        * @brief 将向量偏移从点中减去，并修改原点
        * @param [in,out] ptLHS_ 原点，将被修改
        * @param [in] vOffset_ 减去的向量
        * @return Point2& 修改后的点引用
        */
        _MATH_EXP Point2& operator-=(IN OUT Point2& ptLHS_, IN const Vector2& vOffset_);

        //------------比较运算符-------------
        /**
        * @brief 判断两个点是否相等（按坐标比较）
        * @param [in] ptLHS_ 左操作数
        * @param [in] ptRHS_ 右操作数
        * @return bool 如果 X 和 Y 坐标均相等，返回 true；否则 false
        */
        _MATH_EXP bool operator==(IN const Point2& ptLHS_, IN const Point2& ptRHS_);

        /**
        * @brief 判断两个点的排序关系
        * @param [in] ptLHS_ 左操作数
        * @param [in] ptRHS_ 右操作数
        * @return bool 如果 ptLHS_ 的 X 坐标小于 ptRHS_ 的 X 坐标，或 X 相等且 Y 坐标小于 ptRight_，返回 true；否则 false
        */
        _MATH_EXP bool operator<(IN const Point2& ptLHS_, IN const Point2& ptRHS_);
    }
}

//--------------------------------------
// 容器支持：hash
//--------------------------------------
namespace std
{
    /**
    * @brief std::hash 特化，用于 Point2 在哈希容器中使用
    */
    template<>
    struct hash<iCAX::Math::Point2>
    {
        std::size_t operator()(IN const iCAX::Math::Point2& ptLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<double> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(ptLHS_.X()));
            seed = HashCombine(seed, hasher(ptLHS_.Y()));
            return seed;
        }
    };
}
