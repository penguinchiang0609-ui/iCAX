#pragma once
#include "Math.h"
#include "Point3.h"
#include "Dir3.h"
#include "Tranform3.h"
#include "Quaternion.h"

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 三维轴 (3D Axis)
        * @details
        * 封装三维空间中的轴，由原点和方向向量组成。
        * 方向向量 m_dir 为单位向量，长度代表轴的单位长度。
        */
        struct _MATH_EXP Axis3 final
        {
        public://!< 构造与析构
            /**
            * @brief 默认构造函数
            * @details 初始化原点为 (0,0,0)，方向为 (1,0,0)
            */
            Axis3();

            /**
            * @brief 使用原点和方向构造
            * @param [in] ptLocation_ 轴的原点
            * @param [in] dirX_ 轴的方向（必须归一化）
            */
            Axis3(IN const Point3& ptLocation_, IN const Dir3& dirX_);

        public: //!< 成员访问

            /**
            * @brief 获取轴的原点（只读）
            * @return const Point3& 原点
            */
            const Point3& Location() const;

            /**
            * @brief 获取轴的原点（可写）
            * @return Point3& 原点引用，可修改
            */
            Point3& Location();

            /**
            * @brief 获取轴的方向（只读）
            * @return const Dir3& 方向向量
            */
            const Dir3& Direction() const;

            /**
            * @brief 获取轴的方向（可写）
            * @return Dir3& 方向向量引用，可修改
            */
            Dir3& Direction();

        public: //!< 几何运算

            /**
            * @brief 将点投影到轴上
            * @param [in] ptTarget_ 待投影点
            * @return Point3 投影后的点
            */
            Point3 Project(IN const Point3& ptTarget_) const;

            /**
            * @brief 判断点是否位于轴上
            * @param [in] ptTarget_ 待判断点
            * @param [in] nTol_ 容差，默认 EPSILON
            * @return true 在轴上 / false 不在轴上
            */
            bool IsOn(IN const Point3& ptTarget_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 计算点到轴的最短距离
            * @param [in] ptTarget_ 待计算点
            * @return double 距离
            */
            double Distance(IN const Point3& ptTarget_) const;

            /**
             * @brief 计算点到轴的最短距离平方
             * @param [in] ptTarget_ 待计算点
             * @return double 距离平方
             */
            double DistanceSquared(IN const Point3& ptTarget_) const;

            /**
            * @brief 判断是否与另一条轴几何相等
            * @details 判断两条轴是否重合（共线且重叠）
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差（默认 EPSILON）
            * @return true 表示两条轴几何等价，false 表示不等
            */
            bool IsEqual(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴平行
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示平行，false 表示不平行
            */
            bool IsParallel(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴同向
            * @details 同向是指方向向量平行且夹角小于容差范围。
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示同向，false 表示非同向
            */
            bool IsCodirectional(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴共线
            * @details 同向是指方向向量平行且夹角小于容差范围。
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示同向，false 表示非同向
            */
            bool IsCoaxial(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴反向
            * @details 反向是指方向向量夹角为 180°（即方向取反）
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示反向，false 表示非反向
            */
            bool IsOpposite(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴垂直
            * @details 判断两条轴的方向向量点积是否接近 0
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示垂直，false 表示非垂直
            */
            bool IsPerpendicular(IN const Axis3& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反方向的新轴
            * @details 返回一个方向取反（即方向向量取负）的新轴对象。
            * @return Axis3 新轴对象
            */
            Axis3 Reversed() const;

            /**
            * @brief 当前轴方向取反
            * @details 将当前轴的方向向量取负，改变参数化方向。
            * @return Axis3& 当前轴对象引用
            */
            Axis3& Reverse();

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2
            */
            Axis3 Transformed(IN const Double3& vTranslate_, IN const Quaternion& nRotate_) const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2&
            */
            Axis3& Transform(IN const Double3& vTranslate_, IN const Quaternion& nRotate_);

        private: //!< 成员变量
            Point3 m_ptLocation;  ///< 轴的原点
            Dir3   m_dir;      ///< 轴的方向（单位向量）
        };

        //------------比较运算符-------------

        /**
        * @brief 判断两个轴是否相等
        * @param [in] axLHS_ 左操作数
        * @param [in] axRHS_ 右操作数
        * @return true 相等 / false 不相等
        */
        _MATH_EXP bool operator==(IN const Axis3& axLHS_, IN const Axis3& axRHS_);

        /**
        * @brief 小于比较（用于 map / set 排序）
        * @param [in] axLHS_ 左操作数
        * @param [in] axRHS_ 右操作数
        * @return true axLHS_ < axRHS_
        */
        _MATH_EXP bool operator<(IN const Axis3& axLHS_, IN const Axis3& axRHS_);
    }
}

namespace std
{
    /**
    * @brief std::hash 特化，用于 gAxis3
    * @details 可用于 unordered_map / unordered_set
    */
    template<>
    struct hash<iCAX::Math::Axis3>
    {
        std::size_t operator()(IN const iCAX::Math::Axis3& axLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<iCAX::Math::Point3> pointHasher;
            std::hash<iCAX::Math::Dir3> dirHasher;

            std::size_t seed = 0;
            seed = HashCombine(seed, pointHasher(axLHS_.Location()));
            seed = HashCombine(seed, dirHasher(axLHS_.Direction()));
            return seed;
        }
    };
}
