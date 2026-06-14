#pragma once
#include "Math.h"
#include "Axis2.h"
#include "Vector2.h"
#include "Point2.h"
#include "Dir2.h"

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 二维坐标系类
        * @details
        *  CoordSys2 表示一个二维坐标系，包括原点和 X/Y 方向单位向量。
        *  用于在二维空间中定义局部坐标系和世界坐标系之间的转换。
        */
        struct _MATH_EXP CoordSys2 final
        {
        public://!< 构造与析构
            /**
            * @brief 默认构造函数
            * @details 初始化为默认世界坐标系：
            *  - 原点在 (0,0)
            *  - X方向为 (1,0)
            *  - Y方向为 (0,1)
            */
            CoordSys2();

            /**
            * @brief 构造函数
            * @param ptLocation_ 坐标系原点
            * @param dirX_ X方向单位向量
            * @details 根据原点和 X 方向单位向量构造坐标系，Y方向自动正交生成
            */
            CoordSys2(IN const Point2& ptLocation_, IN const Dir2& dirX_);

            /**
            * @brief 构造函数
            * @param axX_ 坐标系 X 轴
            * @details 根据给定的 X 轴坐标系（原点 + X方向）构造完整二维坐标系
            */
            explicit CoordSys2(IN const Axis2& axX_);

        public: // 成员访问接口
            /**
            * @brief 获取坐标系原点
            * @return const Point2& 原点坐标
            */
            const Point2& Location() const;

            /**
            * @brief 获取 X 方向单位向量
            * @return const Dir2& X 方向单位向量
            */
            const Dir2& XDirection() const;

            /**
            * @brief 获取 Y 方向单位向量
            * @return const Dir2& Y 方向单位向量
            */
            const Dir2& YDirection() const;

            /**
            * @brief 获取 X 轴
            * @return const Axis2& 坐标系 X 轴（原点 + X 方向）
            */
            const Axis2& Axis() const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2
            */
            CoordSys2 Transformed(IN const Double2& vTranslate_, IN const double& nRotate_) const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2&
            */
            CoordSys2& Transform(IN const Double2& vTranslate_, IN const double& nRotate_);

        public: // 坐标变换运算
            /**
            * @brief 获取从局部坐标系到世界坐标系的变换矩阵
            * @return Tranform2 局部到世界的变换
            */
            class Tranform2 LocalToWorld() const;

            /**
            * @brief 获取从世界坐标系到局部坐标系的变换矩阵
            * @return Tranform2 世界到局部的变换
            */
            class Tranform2 WorldToLocal() const;

        private: // 成员变量
            Axis2 m_Axis;   //!< X 轴表示的坐标系（原点 + X 方向）
            Dir2 m_dirY;    //!< Y 方向单位向量
        };

        //------------运算符声明（类外）-------------
        /**
        * @brief 相等比较运算符
        * @param csLHS_ 左操作数
        * @param csRHS_ 右操作数
        * @return true 两个坐标系相同
        */
        _MATH_EXP bool operator==(IN const CoordSys2& csLHS_, IN const CoordSys2& csRHS_);

        /**
        * @brief 小于比较运算符（可用于排序或哈希容器）
        * @param csLHS_ 左操作数
        * @param csRHS_ 右操作数
        * @return true 左操作数小于右操作数
        */
        _MATH_EXP bool operator<(IN const CoordSys2& csLHS_, IN const CoordSys2& csRHS_);
    }
}

namespace std
{
    /**
    * @brief std::hash 特化，用于 CoordSys2 在哈希容器中使用
    */
    template<>
    struct hash<iCAX::Math::CoordSys2>
    {
        std::size_t operator()(IN const iCAX::Math::CoordSys2& csLHS_) const noexcept
        {
            auto _HashCombine = [](std::size_t _nSeed, std::size_t _nValue) {
                return _nSeed ^ (_nValue + 0x9e3779b97f4a7c15ULL + (_nSeed << 6) + (_nSeed >> 2));
                };

            std::hash<iCAX::Math::Point2> _hashPoint;
            std::hash<iCAX::Math::Dir2> _hashXDir;
            std::hash<iCAX::Math::Dir2> _hashYDir;

            std::size_t _nSeed = 0;
            _nSeed = _HashCombine(_nSeed, _hashPoint(csLHS_.Location()));
            _nSeed = _HashCombine(_nSeed, _hashXDir(csLHS_.XDirection()));
            _nSeed = _HashCombine(_nSeed, _hashYDir(csLHS_.YDirection()));
            return _nSeed;
        }
    };
}
