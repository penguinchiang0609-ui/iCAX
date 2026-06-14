#pragma once
#include "Math.h"
#include "Vector2.h"
#include <cmath>
#include <functional>

namespace iCAX
{
    namespace Math
    {
        /**
        * @brief 二维方向向量类
        * @details
        *  Dir2 表示二维空间中的方向向量，始终保持单位长度。
        *  可用于表示坐标系方向、边界方向、几何向量方向等。
        *  内部存储 Vector2，保证归一化。
        */
        class _MATH_EXP Dir2 final
        {
        public://!< 构造与析构
            /**
            * @brief 默认构造函数
            * @details 构造非法方向向量（Nil）
            */
            Dir2();

            /**
            * @brief 由向量构造方向向量（会归一化）
            * @param v_ 输入向量
            */
            explicit Dir2(IN const Vector2& v_);

            /**
            * @brief 由坐标对构造方向向量（会归一化）
            * @param XY_ 输入坐标对
            */
            explicit Dir2(IN const Double2& XY_);

            /**
            * @brief 由坐标值构造方向向量（会归一化）
            * @param nX_ X 分量
            * @param nY_ Y 分量
            */
            Dir2(IN const double& nX_, IN const double& nY_);

            /**
            * @brief 析构函数
            */
            ~Dir2();

        public: // 成员访问
            /**
            * @brief 获取 X 分量
            * @return const double& X 分量
            */
            const double& X() const;

            /**
            * @brief 获取 Y 分量
            * @return const double& Y 分量
            */
            const double& Y() const;

            /**
            * @brief 获取 (X,Y) 坐标对
            * @return const Double2& 坐标对
            */
            const Double2& XY() const;

            /**
            * @brief 获取内部向量（保证单位长度）
            * @return const Vector2& 内部存储的单位向量
            */
            const Vector2& Vector() const;

        public: // 几何关系方法
            /**
            * @brief 计算与另一方向向量的夹角（逆时针）
            * @param dir_ 另一方向向量
            * @return double 弧度值
            */
            double Angle(IN const Dir2& dir_) const;

            /**
            * @brief 获取垂直方向向量（逆时针旋转 90°）
            * @return Dir2 新的垂直方向向量
            */
            Dir2 Perpendicular() const;

            /**
            * @brief 判断是否与另一方向相等
            * @param dir_ 另一方向
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsEqual(IN const Dir2& dir_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否为零向量
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsNil(IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量平行
            * @param dir_ 另一方向
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsParallel(IN const Dir2& dir_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量同向
            * @param dir_ 另一方向
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsCodirectional(IN const Dir2& dir_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量反向
            * @param dir_ 另一方向
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsOpposite(IN const Dir2& dir_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 判断是否与另一向量垂直
            * @param dir_ 另一方向
            * @param nTol_ 公差（默认 EPSILON）
            * @return bool
            */
            bool IsPerpendicular(IN const Dir2& dir_, IN const double& nTol_ = EPSILON) const;

            /**
            * @brief 生成取反方向
            * @details 返回一个方向取反（即方向向量取负）的新直方向。
            * @return Dir2 新方向
            */
            Dir2 Reversed() const;

            /**
            * @brief 当前方向取反
            * @details 将当前方向向量取负，改变参数化方向。
            * @return Dir2& 当前方向对象引用
            */
            Dir2& Reverse();

        public: // 预置常量方向
            /**
            * @brief 获取非法方向向量
            * @return const Dir2& Nil 方向
            */
            static const Dir2& Nil();

            /**
            * @brief 获取左方向 (-1,0)
            * @return const Dir2& Left 方向
            */
            static const Dir2& Left();

            /**
            * @brief 获取右方向 (1,0)
            * @return const Dir2& Right 方向
            */
            static const Dir2& Right();

            /**
            * @brief 获取上方向 (0,1)
            * @return const Dir2& Up 方向
            */
            static const Dir2& Up();

            /**
            * @brief 获取下方向 (0,-1)
            * @return const Dir2& Down 方向
            */
            static const Dir2& Down();

            /**
            * @brief 获取 (1,1) 方向（归一化）
            * @return const Dir2& One 方向
            */
            static const Dir2& One();

        private:
            Vector2 m_vector; ///< 内部存储的单位向量
        };

        //------------比较运算符-------------
        /**
        * @brief 判断两个方向向量是否相等
        * @param dirLHS_ 左操作数
        * @param dirRHS_ 右操作数
        * @return bool
        */
        bool operator==(IN const Dir2& dirLHS_, IN const Dir2& dirRHS_);

        /**
        * @brief 判断两个方向向量的排序（可用于 std::set 等）
        * @param dirLHS_ 左操作数
        * @param dirRHS_ 右操作数
        * @return bool
        */
        bool operator<(IN const Dir2& dirLHS_, IN const Dir2& dirRHS_);
    }
}

namespace std
{
    /**
    * @brief std::hash 特化，用于 Dir2 在哈希容器中使用
    */
    template<>
    struct hash<iCAX::Math::Dir2>
    {
        std::size_t operator()(IN const iCAX::Math::Dir2& dirLHS_) const noexcept
        {
            auto HashCombine = [](std::size_t seed, std::size_t value)
                {
                    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };

            std::hash<iCAX::Math::Vector2> hasher;
            std::size_t _seed = 0;
            _seed = HashCombine(_seed, hasher(dirLHS_.Vector()));
            return _seed;
        }
    };
}
