#pragma once

#include "Math.h"
#include "../Data/Array1.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 二维向量类
        * @details
        *  表示二维空间中的向量，提供基本几何运算、向量运算符以及常用方向常量。
        */
        class _MATH_EXP Vector2 final
        {
        public://!< 构造与析构
            /**
            * @brief 默认构造函数，初始化为零向量 (0,0)
            */
            Vector2();

            /**
            * @brief 由坐标数组构造向量
            * @param nXY_ 坐标数组 {x, y}
            */
            Vector2(IN const Double2& nXY_);

            /**
            * @brief 由 X/Y 坐标构造向量
            * @param nX_ X 分量
            * @param nY_ Y 分量
            */
            Vector2(IN const double& nX_, IN const double& nY_);

            /**
            * @brief 析构函数
            */
            ~Vector2();

        public: //!< 成员访问

            /**
            * @brief 获取或设置 X 分量
            * @return double& X 分量引用
            */
            double& X();

            /**
            * @brief 获取 X 分量
            * @return const double& X 分量
            */
            const double& X() const;

            /**
            * @brief 获取或设置 Y 分量
            * @return double& Y 分量引用
            */
            double& Y();

            /**
            * @brief 获取 Y 分量
            * @return const double& Y 分量
            */
            const double& Y() const;

            /**
            * @brief 获取或设置向量坐标对
            * @return Double2& {X, Y} 坐标对
            */
            Double2& XY();

            /**
            * @brief 获取向量坐标对
            * @return const Double2& {X, Y} 坐标对
            */
            const Double2& XY() const;

            /*
            * @brief 判断是否为非法向量
            * @return bool
            */
            bool IsNil() const;

        public: // 几何运算

            /**
            * @brief 计算与另一向量的点积
            * @param vOther_ 另一向量
            * @return double 点积结果
            */
            double Dot(IN const Vector2& vOther_) const;

            /**
            * @brief 计算与另一向量的二维叉积（标量）
            * @param vOther_ 另一向量
            * @return double 叉积结果
            */
            double Cross(IN const Vector2& vOther_) const;

            /**
            * @brief 计算向量模长
            * @return double 向量长度
            */
            double Magnitude() const;

            /**
            * @brief 计算向量模长的平方
            * @return double 向量长度平方
            */
            double MagnitudeSquared() const;

            /**
            * @brief 返回单位化后的向量，不改变自身
            * @return Vector2 单位化向量
            */
            Vector2 Normalized() const;

            /**
            * @brief 将向量本身单位化
            * @return Vector2& 自身引用
            */
            Vector2& Normalize();

            /**
            * @brief 计算与另一向量的夹角（逆时针方向，弧度）
            * @param vOther_ 另一向量
            * @return double 弧度值
            */
            double Angle(IN const Vector2& vOther_) const;

            /**
            * @brief 获取逆时针旋转 90° 的垂直向量
            * @return Vector2 垂直向量
            */
            Vector2 Perpendicular() const;

            /**
            * @brief 判断是否为单位向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsNormalized(IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否为零向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsZero(IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量相等
            * @param vOther_ 另一向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsEqual(IN const Vector2& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量平行
            * @param vOther_ 另一向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsParallel(IN const Vector2& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量同向
            * @param vOther_ 另一向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsCodirectional(IN const Vector2& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量反向
            * @param vOther_ 另一向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsOpposite(IN const Vector2& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量垂直
            * @param vOther_ 另一向量
            * @param nTol_ 容差
            * @return bool
            */
            bool IsPerpendicular(IN const Vector2& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反向量
            * @details 返回一个方向取反（即方向向量取负）的新向量。
            * @return Vector2 新向量
            */
            Vector2 Reversed() const;

            /**
            * @brief 当前向量取反
            * @details 将当前向量方向取负，改变参数化方向。
            * @return Vector2&
            */
            Vector2& Reverse();

        public: // 预置变量

            /**
            * @brief 获取零向量 (0,0)
            * @return const Vector2& 零向量
            */
            static const Vector2& Zero();

            /**
            * @brief 获取左向量 (-1,0)
            * @return const Vector2& 左向量
            */
            static const Vector2& Left();

            /**
            * @brief 获取右向量 (1,0)
            * @return const Vector2& 右向量
            */
            static const Vector2& Right();

            /**
            * @brief 获取上向量 (0,1)
            * @return const Vector2& 上向量
            */
            static const Vector2& Up();

            /**
            * @brief 获取下向量 (0,-1)
            * @return const Vector2& 下向量
            */
            static const Vector2& Down();

            /**
            * @brief 获取单位向量 (1,1)
            * @return const Vector2& 单位向量
            */
            static const Vector2& One();

            /*
            * @brief 获取非法向量(NaN, NaN)
            * @return const Vector2&
            */
            static const Vector2& Nil();

        private:
            Double2 m_nXY; ///< 内部存储的向量坐标
        };

        //------------算术运算符-------------

        /**
        * @brief 向量加法
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return Vector2 两个向量相加后的新向量
        */
        _MATH_EXP Vector2 operator+(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量减法
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return Vector2 两个向量相减后的新向量
        */
        _MATH_EXP Vector2 operator-(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量点乘
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return double 两个向量的点积
        */
        _MATH_EXP double operator*(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量叉乘（二维标量）
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return double 二维向量的叉积标量
        */
        _MATH_EXP double operator^(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量乘标量
        * @param [in] vLHS_ 待放大的向量
        * @param [in] nFactor_ 标量因子
        * @return Vector2 放大后的向量
        */
        _MATH_EXP Vector2 operator*(IN const Vector2& vLHS_, IN const double& nFactor_);

        /**
        * @brief 标量乘向量
        * @param [in] nFactor_ 标量因子
        * @param [in] vRHS_ 待放大的向量
        * @return Vector2 放大后的向量
        */
        _MATH_EXP Vector2 operator*(IN const double& nFactor_, IN const Vector2& vRHS_);

        /**
        * @brief 向量除标量
        * @param [in] vLHS_ 待缩放的向量
        * @param [in] nFactor_ 标量因子
        * @return Vector2 缩放后的向量
        */
        _MATH_EXP Vector2 operator/(IN const Vector2& vLHS_, IN const double& nFactor_);

        /**
        * @brief 向量加等
        * @param [in,out] vLHS_ 左操作数向量，将被修改
        * @param [in] vRHS_ 右操作数向量
        * @return Vector2& 修改后的左操作数引用
        */
        _MATH_EXP Vector2& operator+=(IN OUT Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量减等
        * @param [in,out] vLHS_ 左操作数向量，将被修改
        * @param [in] vRHS_ 右操作数向量
        * @return Vector2& 修改后的左操作数引用
        */
        _MATH_EXP Vector2& operator-=(IN OUT Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量乘标量等
        * @param [in,out] vLHS_ 待修改向量
        * @param [in] nFactor_ 标量因子
        * @return Vector2& 修改后的向量引用
        */
        _MATH_EXP Vector2& operator*=(IN OUT Vector2& vLHS_, IN const double& nFactor_);

        /**
        * @brief 向量除标量等
        * @param [in,out] vLHS_ 待修改向量
        * @param [in] nFactor_ 标量因子
        * @return Vector2& 修改后的向量引用
        */
        _MATH_EXP Vector2& operator/=(IN OUT Vector2& vLHS_, IN const double& nFactor_);

        /**
        * @brief 取反向量
        * @param [in] vLHS_ 原向量
        * @return Vector2 反向的新向量
        */
        _MATH_EXP Vector2 operator-(IN const Vector2& vLHS_);

        //------------比较运算符-------------

        /**
        * @brief 向量相等比较
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return bool 如果 X/Y 分量相等则返回 true，否则 false
        */
        _MATH_EXP bool operator==(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

        /**
        * @brief 向量小于比较（用于排序/映射）
        * @param [in] vLHS_ 左操作数向量
        * @param [in] vRHS_ 右操作数向量
        * @return bool 如果 X 分量小，或 X 相等且 Y 分量小，则返回 true；否则 false
        */
        _MATH_EXP bool operator<(IN const Vector2& vLHS_, IN const Vector2& vRHS_);

    }
}

namespace std
{
    /**
     * @brief std::hash 特化，用于 Vector2 在哈希容器中使用
     */
    template<>
    struct hash<iCAX::Math::Vector2>
    {
        std::size_t operator()(IN const iCAX::Math::Vector2& vLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<double> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(vLHS_.X()));
            seed = HashCombine(seed, hasher(vLHS_.Y()));
            return seed;
        }
    };
}
