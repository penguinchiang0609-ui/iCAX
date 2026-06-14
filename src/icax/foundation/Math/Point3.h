#pragma once

#include "Math.h"
#include "../Data/Array1.h"
#include "Vector3.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维点类
        * @details
        *   表示三维空间中的一个点。
        *   支持常见的点运算、距离计算、线性插值、权重组合等。
        *   与 Vector3 不同，gPoint3 表示位置而非方向。
        */
        class _MATH_EXP Point3 final
        {
        public://!< 构造与析构
            /*
            * @brief 默认构造函数
            * @remark 初始化为 (0,0,0)
            */
            Point3();

            /*
            * @brief 使用 Double3 构造
            * @param [in] nXYZ_ 三维坐标数组
            */
            Point3(IN const Double3& nXYZ_);

            /*
            * @brief 使用 X, Y, Z 构造
            * @param [in] nX_ X 坐标
            * @param [in] nY_ Y 坐标
            * @param [in] nZ_ Z 坐标
            */
            Point3(IN const double& nX_, IN const double& nY_, IN const double& nZ_);

        public://!< 成员访问
            /*
            * @brief 获取 X 分量
            * @return double& X 分量引用
            */
            double& X();

            /*
            * @brief 获取 X 分量（常量版本）
            * @return const double& X 分量常量引用
            */
            const double& X() const;

            /*
            * @brief 获取 Y 分量
            * @return double& Y 分量引用
            */
            double& Y();

            /*
            * @brief 获取 Y 分量（常量版本）
            * @return const double& Y 分量常量引用
            */
            const double& Y() const;

            /*
            * @brief 获取 Z 分量
            * @return double& Z 分量引用
            */
            double& Z();

            /*
            * @brief 获取 Z 分量（常量版本）
            * @return const double& Z 分量常量引用
            */
            const double& Z() const;

            /*
            * @brief 获取 (X,Y,Z) 坐标
            * @return Double3& 坐标引用
            */
            Double3& XYZ();

            /*
            * @brief 获取 (X,Y,Z) 坐标（常量版本）
            * @return const Double3& 坐标常量引用
            */
            const Double3& XYZ() const;

            /*
            * @brief 判断是否为非法点
            * @return bool
            */
            bool IsNil() const;

        public://!< 几何运算
            /*
            * @brief 点的线性插值
            * @param [in] ptTarget_ 目标点
            * @param [in] nFactor_ 插值因子 [0,1]
            * @return Point3 插值结果点
            */
            Point3 Lerp(IN const Point3& ptTarget_, IN const double& nFactor_) const;

            /*
            * @brief 判断点是否相等（带容差）
            * @param [in] ptOther_ 比较点
            * @param [in] nTol_ 容差（默认 EPSILON）
            * @return bool 若相等则返回 true
            */
            bool IsEqual(IN const Point3& ptOther_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 计算两点之间的欧氏距离
            * @param [in] ptOther_ 目标点
            * @return double 距离值
            */
            double Distance(IN const Point3& ptOther_) const;

            /*
            * @brief 计算两点之间的平方距离
            * @param [in] ptOther_ 目标点
            * @return double 距离平方值
            */
            double DistanceSquared(IN const Point3& ptOther_) const;

            /*
            * @brief 按权重组合两个点
            * @param [in] ptOther_ 目标点
            * @param [in] nWeight1_ 当前点权重
            * @param [in] nWeight2_ 目标点权重
            * @return Point3 加权组合后的点
            */
            Point3 Combine(IN const Point3& ptOther_, IN const double& nWeight1_, IN const double& nWeight2_) const;

        public://!< 常量点
            /*
            * @brief 获取零点 (0, 0, 0)
            * @return const Point3& 静态实例
            */
            static const Point3& Zero();

            /*
            * @brief 获取一 (1, 1, 1)
            * @return const Point3& 静态实例
            */
            static const Point3& One();

            /*
            * @brief 获取右点 (1, 0, 0)
            * @return const Point3& 静态实例
            */
            static const Point3& Right();

            /*
            * @brief 获取左点 (-1, 0, 0)
            * @return const Point3& 静态实例
            */
            static const Point3& Left();

            /*
            * @brief 获取上点 (0, 0, 1)
            * @return const Point3& 静态实例
            */
            static const Point3& Up();

            /*
            * @brief 获取下点 (0, 0, -1)
            * @return const Point3& 静态实例
            */
            static const Point3& Down();

            /*
            * @brief 获取前点 (0, 1, 0)
            * @return const Point3& 静态实例
            */
            static const Point3& Forward();

            /*
            * @brief 获取后点 (0, -1, 0)
            * @return const Point3& 静态实例
            */
            static const Point3& Back();

            /*
            * @brief 获取非法点(NaN, NaN)
            * @return const Point2&
            */
            static const Point3& Nil();

        private:
            Double3 m_nXYZ;  //!< 点的三维坐标 (X, Y, Z)
        };

        //------------算术运算符-------------

        /*
        * @brief 点 + 向量 = 点
        * @param [in] ptLHS_ 原点
        * @param [in] vOffset_ 位移向量
        * @return Point3 平移后的点
        */
        _MATH_EXP Point3 operator+(IN const Point3& ptLHS_, IN const Vector3& vOffset_);

        /*
        * @brief 点 - 向量 = 点
        * @param [in] ptLHS_ 原点
        * @param [in] vOffset_ 位移向量
        * @return Point3 平移后的点
        */
        _MATH_EXP Point3 operator-(IN const Point3& ptLHS_, IN const Vector3& vOffset_);

        /*
        * @brief 点 - 点 = 向量
        * @param [in] ptLHS_ 左操作数（当前点）
        * @param [in] ptRHS_ 右操作数（目标点）
        * @return Vector3 表示从右点到左点的向量
        */
        _MATH_EXP Vector3 operator-(IN const Point3& ptLHS_, IN const Point3& ptRHS_);

        /*
        * @brief 点 += 向量
        * @param [in,out] ptLHS_ 被修改的点
        * @param [in] vOffset_ 位移向量
        * @return Point3& 修改后的点
        */
        _MATH_EXP Point3& operator+=(IN OUT Point3& ptLHS_, IN const Vector3& vOffset_);

        /*
        * @brief 点 -= 向量
        * @param [in,out] ptLHS_ 被修改的点
        * @param [in] vOffset_ 位移向量
        * @return Point3& 修改后的点
        */
        _MATH_EXP Point3& operator-=(IN OUT Point3& ptLHS_, IN const Vector3& vOffset_);

        //------------比较运算符-------------

        /*
        * @brief 判断两点是否相等
        * @param [in] ptLHS_ 左操作数
        * @param [in] ptRHS_ 右操作数
        * @return bool 若坐标完全相等则返回 true
        */
        _MATH_EXP bool operator==(IN const Point3& ptLHS_, IN const Point3& ptRHS_);

        /*
        * @brief 小于比较（用于排序或映射）
        * @param [in] ptLHS_ 左操作数
        * @param [in] ptRHS_ 右操作数
        * @return bool 若左点的字典序小于右点则返回 true
        */
        _MATH_EXP bool operator<(IN const Point3& ptLHS_, IN const Point3& ptRHS_);
    }
}

namespace std
{
    /*
    * @brief Point3 的哈希函数特化
    * @details
    *   支持 Point3 作为 unordered_map、unordered_set 的键使用。
    */
    template<>
    struct hash<iCAX::Math::Point3>
    {
        std::size_t operator()(IN const iCAX::Math::Point3& ptLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value)
                {
                    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<double> hasher;
            std::size_t seed = 0;
            seed = HashCombine(seed, hasher(ptLHS_.X()));
            seed = HashCombine(seed, hasher(ptLHS_.Y()));
            seed = HashCombine(seed, hasher(ptLHS_.Z()));
            return seed;
        }
    };
}
