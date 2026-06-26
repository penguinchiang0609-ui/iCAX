#pragma once
#include "Math.h"
#include "Point2.h"
#include "Dir2.h"
#include "Tranform2.h"

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 二维轴
        * @details
        *   - 使用 Dir2 表示方向，长度为单位向量
        *   - 提供几何运算和变换方法
        *   - 可用于点投影、距离计算、点是否在轴上判断等操作
        */
        struct _MATH_EXP Axis2 final
        {
        public://!< 构造与析构
            /*
            * @brief 默认构造函数
            * @details 原点初始化为 (0,0)，方向初始化为 (1,0)
            */
            Axis2();

            /*
            * @brief 构造函数
            * @param [in] ptLocation_ 轴原点
            * @param [in] vDir_ 轴方向（单位向量）
            * @throw 如果方向向量为零抛出异常
            */
            Axis2(IN const Point2& ptLocation_, IN const Dir2& vDir_);

        public: //!< 成员访问
            /*
            * @brief 获取原点（常量版本）
            * @return const Point2& 原点坐标
            */
            const Point2& Location() const;

            /*
            * @brief 获取原点
            * @return Point2& 原点坐标
            */
            Point2& Location();

            /*
            * @brief 获取方向（常量版本）
            * @return const Dir2& 轴方向（单位向量）
            */
            const Dir2& Direction() const;

            /*
            * @brief 获取方向
            * @return Dir2& 轴方向（单位向量）
            */
            Dir2& Direction();

        public: //!< 几何运算
            /*
            * @brief 将点投影到轴上
            * @param [in] ptTarget_ 目标点
            * @return Point2 投影点
            */
            Point2 Project(IN const Point2& ptTarget_) const;

            /*
            * @brief 判断点是否在轴上
            * @param [in] ptTarget_ 目标点
            * @param [in] nTol_ 容差值，默认 EPSILON
            * @return bool 点是否在轴上
            */
            bool IsOn(IN const Point2& ptTarget_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 计算点到轴的距离
            * @param [in] ptTarget_ 目标点
            * @return double 距离值
            */
            double Distance(IN const Point2& ptTarget_) const;

            /*
            * @brief 计算点到轴的平方距离
            * @param [in] ptTarget_ 目标点
            * @return double 平方距离值
            */
            double DistanceSquared(IN const Point2& ptTarget_) const;

            /**
            * @brief 判断是否与另一条轴几何相等
            * @details 判断两条轴是否重合（共线且重叠）
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差（默认 EPSILON）
            * @return true 表示两条轴几何等价，false 表示不等
            */
            bool IsEqual(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴平行
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示平行，false 表示不平行
            */
            bool IsParallel(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴同向
            * @details 同向是指方向向量平行且夹角小于容差范围。
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示同向，false 表示非同向
            */
            bool IsCodirectional(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴共线
            * @details 同向是指方向向量平行且夹角小于容差范围。
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示同向，false 表示非同向
            */
            bool IsCoaxial(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴反向
            * @details 反向是指方向向量夹角为 180°（即方向取反）
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示反向，false 表示非反向
            */
            bool IsOpposite(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一条轴垂直
            * @details 判断两条轴的方向向量点积是否接近 0
            * @param [in] axOther_ 另一条轴
            * @param [in] nTol_ 比较容差
            * @return true 表示垂直，false 表示非垂直
            */
            bool IsPerpendicular(IN const Axis2& axOther_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反方向的新轴
            * @details 返回一个方向取反（即方向向量取负）的新轴对象。
            * @return Axis2 新轴对象
            */
            Axis2 Reversed() const;

            /**
            * @brief 当前轴方向取反
            * @details 将当前轴的方向向量取负，改变参数化方向。
            * @return Axis2& 当前轴对象引用
            */
            Axis2& Reverse();

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2
            */
            Axis2 Transformed(IN const Double2& vTranslate_, IN const double& nRotate_) const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2&
            */
            Axis2& Transform(IN const Double2& vTranslate_, IN const double& nRotate_);

        private:
            Point2 m_ptLocation;   //!< 轴原点
            Dir2   m_dir;       //!< 轴方向（单位向量）
        };

        //------------比较运算符-------------
        /*
        * @brief 判断两个二维轴是否相等
        * @param [in] axLHS_ 左操作数
        * @param [in] axRHS_ 右操作数
        * @return bool 是否相等
        */
        _MATH_EXP bool operator==(IN const Axis2& axLHS_, IN const Axis2& axRHS_);

        /*
        * @brief 小于比较，用于 std::map 排序
        * @param [in] axLHS_ 左操作数
        * @param [in] axRHS_ 右操作数
        * @return bool 是否小于
        */
        _MATH_EXP bool operator<(IN const Axis2& axLHS_, IN const Axis2& axRHS_);
    }
}

namespace std
{
    /**
    * @brief std::hash 特化，用于 gpAxis2 在哈希容器中使用
    */
    template<>
    struct hash<iCAX::Math::Axis2>
    {
        std::size_t operator()(IN const iCAX::Math::Axis2& axLHS_) const noexcept
        {
            auto _HashCombine = [](std::size_t _nSeed, std::size_t _nValue) {
                return _nSeed ^ (_nValue + 0x9e3779b97f4a7c15ULL + (_nSeed << 6) + (_nSeed >> 2));
                };

            std::hash<iCAX::Math::Point2> _hashPoint;
            std::hash<iCAX::Math::Dir2> _hashDir;

            std::size_t _nSeed = 0;
            _nSeed = _HashCombine(_nSeed, _hashPoint(axLHS_.Location()));
            _nSeed = _HashCombine(_nSeed, _hashDir(axLHS_.Direction()));
            return _nSeed;
        }
    };
}
