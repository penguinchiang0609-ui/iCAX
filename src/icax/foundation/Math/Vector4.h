#pragma once

#include "Math.h"
#include "../Data/Array1.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 四维向量类
        * @details
        * Vector4 表示四维向量 (X, Y, Z, W)。
        * 可用于齐次坐标、四元数或任意四维向量运算。
        */
        class _MATH_EXP Vector4 final
        {
        public:
            /*
            * @brief 默认构造函数
            * @details 将向量初始化为 (0, 0, 0, 0)。
            */
            Vector4();

            /*
            * @brief 构造函数
            * @param [in] nXYZW_ 四维向量数组 (Double4)
            */
            Vector4(IN const Double4& nXYZW_);

            /*
            * @brief 构造函数
            * @param [in] nX_ X 分量
            * @param [in] nY_ Y 分量
            * @param [in] nZ_ Z 分量
            * @param [in] nW_ W 分量
            */
            Vector4(IN const double& nX_, IN const double& nY_, IN const double& nZ_, IN const double& nW_);

            /*
            * @brief 析构函数
            */
            ~Vector4();

        public://!< 成员访问
            /*
            * @brief 获取或修改 X 分量
            * @return double& 返回 X 分量引用，可修改
            */
            double& X();
            /*
            * @brief 获取或修改 X 分量
            * @return const double& 返回 X 分量引用，只读
            */
            const double& X() const;

            /*
            * @brief 获取或修改 Y 分量
            * @return double& 返回 Y 分量引用，可修改
            */
            double& Y();
            /*
            * @brief 获取或修改 Y 分量
            * @return const double& 返回 Y 分量引用，只读
            */
            const double& Y() const;

            /*
            * @brief 获取或修改 Z 分量
            * @return double& 返回 Z 分量引用，可修改
            */
            double& Z();
            /*
            * @brief 获取或修改 Z 分量
            * @return const double& 返回 Z 分量引用，只读
            */
            const double& Z() const;

            /*
            * @brief 获取或修改 W 分量
            * @return double& 返回 W 分量引用，可修改
            */
            double& W();
            /*
            * @brief 获取或修改 W 分量
            * @return const double& 返回 W 分量引用，只读
            */
            const double& W() const;

            /*
            * @brief 获取整个四维向量的引用
            * @return Double4& 返回向量数组引用，可修改
            */
            Double4& XYZW();
            const Double4& XYZW() const;

        public://!< 几何方法
            /*
            * @brief 计算向量模长
            * @return double 向量长度 sqrt(X^2 + Y^2 + Z^2 + W^2)
            */
            double Magnitude() const;

            /*
            * @brief 计算向量模长平方
            * @return double 向量长度平方 X^2 + Y^2 + Z^2 + W^2
            */
            double MagnitudeSquared() const;

            /*
            * @brief 获取单位化向量
            * @return Vector4 返回归一化后的向量，不修改自身
            */
            Vector4 Normalized() const;

            /*
            * @brief 将向量归一化
            * @return Vector4& 返回自身引用，已归一化
            */
            Vector4& Normalize();

            /*
            * @brief 判断向量是否单位化
            * @return bool 若长度接近 1 返回 true，否则 false
            */
            bool IsNormalized() const;

        private:
            Double4 m_nXYZW; //!< 四维向量存储 (X,Y,Z,W)
        };

        /*
        * @brief 判断两个向量是否相等
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return bool 两向量每个分量相等返回 true，否则 false
        */
        _MATH_EXP bool operator==(IN const Vector4& vLHS_, IN const Vector4& vRHS_);

        /*
        * @brief 小于比较运算符
        * @details 常用于排序或容器索引比较，按分量顺序比较 X, Y, Z, W
        * @param [in] vLHS_ 左操作数
        * @param [in] vRHS_ 右操作数
        * @return bool 若 Left_ < Right_ 返回 true，否则 false
        */
        _MATH_EXP bool operator<(IN const Vector4& vLHS_, IN const Vector4& vRHS_);
    }
}
