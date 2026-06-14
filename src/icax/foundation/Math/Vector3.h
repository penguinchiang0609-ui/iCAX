#pragma once

#include "Math.h"
#include "../Data/Array1.h"

using namespace iCAX::Data;
namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维向量类
        * @details
        *   表示一个三维向量 (x, y, z)，用于几何计算、空间变换等操作。
        */
        class _MATH_EXP Vector3 final
        {
        public:
            /*
            * @brief 默认构造函数
            * @remark 初始化为零向量 (0,0,0)
            */
            Vector3();

            /*
            * @brief 使用 Double3 构造
            * @param [in] nXYZ_ 三维坐标数组
            */
            Vector3(IN const Double3& nXYZ_);
            /*
            * @brief 使用 X, Y, Z 构造
            * @param [in] nX_ X 分量
            * @param [in] nY_ Y 分量
            * @param [in] nZ_ Z 分量
            */
            Vector3(IN const double& nX_, IN const double& nY_, IN const double& nZ_);
            /*
            * @brief 析构函数
            */
            ~Vector3();

        public://!< 成员访问
            /*
            * @brief X分量
            * @return double&
            */
            double& X();
            /*
            * @brief X分量
            * @return const double&
            */
            const double& X() const;
            /*
            * @brief Y分量
            * @return double&
            */
            double& Y();
            /*
            * @brief Y分量
            * @return const double&
            */
            const double& Y() const;
            /*
            * @brief Z分量
            * @return double&
            */
            double& Z();
            /*
            * @brief Z分量
            * @return const double&
            */
            const double& Z() const;
            /*
            * @brief XYZ
            * @return Double3&
            */
            Double3& XYZ();
            /*
            * @brief XYZ
            * @return const Double3&
            */
            const Double3& XYZ() const;

            /*
            * @brief 判断是否为非法向量
            * @return bool
            */
            bool IsNil() const;

        public://!< 几何运算
            /*
            * @brief 点积
            * @param [in] vOther_ 另一个向量
            * @return double 点积结果
            */
            double Dot(IN const Vector3& vOther_) const;
            /*
            * @brief 叉积
            * @param [in] vOther_ 另一个向量
            * @return Vector3 叉积结果向量
            */
            Vector3 Cross(IN const Vector3& vOther_) const;
            /*
            * @brief 模长
            * @return double 向量长度
            */
            double Magnitude() const;
            /*
            * @brief 模长平方
            * @return double 向量长度的平方
            */
            double MagnitudeSquared() const;
            /*
            * @brief 单位化后的向量
            * @return Vector3 返回新向量，不修改自身
            */
            Vector3 Normalized() const;
            /*
            * @brief 将向量单位化
            * @return Vector3& 修改自身，返回引用
            */
            Vector3& Normalize();
            /*
            * @brief 与另一个向量的夹角
            * @param [in] vOther_ 向量
            * @return double 夹角（弧度）
            */
            double Angle(IN const Vector3& vOther_) const;
            /*
            * @brief 判断是否为单位向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsNormalized(IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 判断是否为零向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsZero(IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 判断两个向量是否相等
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsEqual(IN const Vector3& vOther_, IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 是否平行
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsParallel(IN const Vector3& vOther_, IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 是否同向
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsCodirectional(IN const Vector3& vOther_, IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 是否反向
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsOpposite(IN const Vector3& vOther_, IN const double& nTol_ = EPSILON) const;
            /*
            * @brief 是否垂直
            * @param [in] vOther_ 向量
            * @param [in] nTol_ 容差
            * @return bool
            */
            bool IsPerpendicular(IN const Vector3& vOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反向量
            * @details 返回一个方向取反（即方向向量取负）的新向量。
            * @return Vector3 新向量
            */
            Vector3 Reversed() const;

            /**
            * @brief 当前向量取反
            * @details 将当前向量方向取负，改变参数化方向。
            * @return Vector3&
            */
            Vector3& Reverse();

        public:
            /*
            * @brief 获取零向量
            * @return const Vector3&
            */
            static const Vector3& Zero();
            /*
            * @brief 获取 (1, 1, 1) 向量
            * @return const Dir3&
            */
            static const Vector3& One();
            /*
            * @brief 获取右向量 (1, 0, 0)
            * @return const Dir3&
            */
            static const Vector3& Right();
            /*
            * @brief 获取左向量 (-1, 0, 0)
            * @return const Dir3&
            */
            static const Vector3& Left();
            /*
            * @brief 获取上向量 (0, 0, 1)
            * @return const Dir3&
            */
            static const Vector3& Up();
            /*
            * @brief 获取下向量 (0, 0, -1)
            * @return const Dir3&
            */
            static const Vector3& Down();
            /*
            * @brief 获取前向量 (0, 1, 0)
            * @return const Dir3&
            */
            static const Vector3& Forward();
            /*
            * @brief 获取后向量 (0, -1, 0)
            * @return const Dir3&
            */
            static const Vector3& Back();

            /*
            * @brief 获取非法向量(NaN, NaN)
            * @return const Vector3&
            */
            static const Vector3& Nil();

        private:
            Double3 m_nXYZ;
        };


        //------------算术运算符-------------
        /*
        * @brief 向量相加
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return Vector3 结果向量
        * @remark 对应分量相加，返回新向量
        */
        _MATH_EXP Vector3 operator+(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量相减
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return Vector3 结果向量
        * @remark 对应分量相减，返回新向量
        */
        _MATH_EXP Vector3 operator-(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量点乘
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return double 点积结果
        * @remark 计算公式：l_.X()*vRHS_.X() + vLHS_.Y()*vRHS_.Y() + vLHS_.Z()*r_.Z()
        */
        _MATH_EXP double operator*(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量叉乘
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return Vector3 结果向量
        * @remark 结果为垂直于两个向量的向量
        */
        _MATH_EXP Vector3 operator^(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量乘以标量
        * @param [in] vLHS_ 向量
        * @param [in] nFactor_ 标量
        * @return Vector3 结果向量
        * @remark 每个分量乘以相同的标量
        */
        _MATH_EXP Vector3 operator*(IN const Vector3& vLHS_, IN const double& nFactor_);

        /*
        * @brief 标量乘以向量
        * @param [in] nFactor_ 标量
        * @param [in] vRHS_ 向量
        * @return Vector3 结果向量
        * @remark 等价于 vRHS_ * n_
        */
        _MATH_EXP Vector3 operator*(IN const double& nFactor_, IN const Vector3& vRHS_);

        /*
        * @brief 向量除以标量
        * @param [in] vLHS_ 向量
        * @param [in] nFactor_ 标量
        * @return Vector3 结果向量
        * @remark 每个分量除以标量 nFactor_，nFactor_ 不可为 0
        */
        _MATH_EXP Vector3 operator/(IN const Vector3& vLHS_, IN const double& nFactor_);

        /*
        * @brief 向量加等
        * @param [in,out] vLHS_ 左操作数，结果将存入其中
        * @param [in] vRHS_ 右操作数
        * @return Vector3& 左操作数引用
        * @remark 对应分量相加并赋值给左操作数
        */
        _MATH_EXP Vector3& operator+=(IN Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量减等
        * @param [in,out] vLHS_ 左操作数，结果将存入其中
        * @param [in] vRHS_ 右操作数
        * @return Vector3& 左操作数引用
        * @remark 对应分量相减并赋值给左操作数
        */
        _MATH_EXP Vector3& operator-=(IN Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量乘标量等
        * @param [in,out] vLHS_ 向量，结果将存入其中
        * @param [in] nFactor_ 标量
        * @return Vector3& 向量自身引用
        * @remark 每个分量乘以标量 nFactor_
        */
        _MATH_EXP Vector3& operator*=(IN Vector3& vLHS_, IN const double& nFactor_);

        /*
        * @brief 向量除标量等
        * @param [in,out] vLHS_ 向量，结果将存入其中
        * @param [in] nFactor_ 标量
        * @return Vector3& 向量自身引用
        * @remark 每个分量除以标量 nFactor_，nFactor_ 不可为 0
        */
        _MATH_EXP Vector3& operator/=(IN Vector3& vLHS_, IN const double& nFactor_);

        /*
        * @brief 向量取反
        * @param [in] vLHS_ 向量
        * @return Vector3 反向后的向量
        * @remark 结果为 (-X, -Y, -Z)
        */
        _MATH_EXP Vector3 operator-(IN const Vector3& vLHS_);


        //------------比较运算符-------------
        /*
        * @brief 向量相等比较
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return bool 是否相等
        * @remark 当各分量差值小于 EPSILON 时认为相等
        */
        _MATH_EXP bool operator==(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

        /*
        * @brief 向量小于比较（用于排序 / 映射）
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return bool 比较结果
        * @remark 按 X、Y、Z 的字典序进行比较
        */
        _MATH_EXP bool operator<(IN const Vector3& vLHS_, IN const Vector3& vRHS_);

    }
}

namespace std
{
    /**
    * @brief std::hash 特化，用于 Vector3
    * @details 可用于 unordered_map / unordered_set
    */
    template<>
    struct hash<iCAX::Math::Vector3>
    {
        std::size_t operator()(IN const iCAX::Math::Vector3& vLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<double> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(vLHS_.X()));
            seed = HashCombine(seed, hasher(vLHS_.Y()));
            seed = HashCombine(seed, hasher(vLHS_.Z()));
            return seed;
        }
    };
}