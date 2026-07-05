#pragma once
#include "Math.h"
#include "Axis3.h"
#include "Vector3.h"
#include "Tranform3.h"

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维坐标系
        * @details
        *   表示三维空间中的一个局部坐标系，由原点、X轴、Y轴、Z轴构成。
        *   通过 Axis3 和两个方向向量来定义，支持坐标系的几何变换。
        */
        struct _MATH_EXP CoordSys3 final
        {
        public:
            /*
            * @brief 默认构造函数
            * @remark 初始化为世界坐标系：
            *         原点在 (0,0,0)，X=(1,0,0)，Y=(0,1,0)，Z=(0,0,1)
            */
            CoordSys3();

            /*
            * @brief 使用原点与两个方向向量构造坐标系
            * @param [in] ptLocation_ 原点坐标
            * @param [in] dirX_ X 方向向量
            * @remark
            *   Z 方向通过 X、Y 方向向量的叉乘推导而得。
            *   用于定义任意旋转或倾斜的局部坐标系。
            */
            CoordSys3(IN const Point3& ptLocation_, IN const Dir3 dirX_);

            /*
            * @brief 使用原点与两个方向向量构造坐标系
            * @param [in] ptLocation_ 原点坐标
            * @param [in] dirX_ X 方向向量
            * @param [in] dirY_ Y 方向向量
            * @remark
            *   Z 方向通过 X、Y 方向向量的叉乘推导而得。
            *   用于定义任意旋转或倾斜的局部坐标系。
            */
            CoordSys3(IN const Point3& ptLocation_, IN const Dir3 dirX_, IN const Dir3 dirY_);

            /*
            * @brief 使用 X 轴与 Y 方向构造坐标系
            * @param [in] axX_ 坐标系的 X 轴（包含原点与方向）
            * @param [in] dirY_ Y 方向向量
            * @remark
            *   根据 X 轴与 Y 方向向量自动计算 Z 方向，
            *   常用于已知旋转轴情况下的局部坐标定义。
            */
            CoordSys3(IN const Axis3& axX_, IN const Dir3& dirY_);

        public: //!< 成员访问
            /*
            * @brief 获取坐标系原点
            * @return const Point3& 原点坐标
            */
            const Point3& Location() const;

            /*
            * @brief 获取 X 方向
            * @return const Dir3& X 方向向量
            */
            const Dir3& XDirection() const;

            /*
            * @brief 获取 Y 方向
            * @return const Dir3& Y 方向向量
            */
            const Dir3& YDirection() const;

            /*
            * @brief 获取 Z 方向
            * @return const Dir3& Z 方向向量
            */
            const Dir3& ZDirection() const;

            /*
            * @brief 获取坐标系的 X 轴
            * @return const Axis3& X 轴（包含原点与方向）
            */
            const Axis3& Axis() const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2
            */
            CoordSys3 Transformed(IN const Double3& vTranslate_, IN const Quaternion& nRotate_) const;

            /*
            * @brief 变换
            * @param [in] vTranslate_
            * @param [in] nRotate_
            * @return CoordSys2&
            */
            CoordSys3& Transform(IN const Double3& vTranslate_, IN const Quaternion& nRotate_);

        public: //!< 几何运算
            /*
            * @brief 获取当前坐标系到世界坐标系的变换
            * @return Tranform3 坐标变换对象
            * @remark 用于将局部空间的点或向量转换到世界空间。
            */
            Tranform3 LocalToWorld() const;

            /*
            * @brief 获取世界坐标系到当前坐标系的变换
            * @return Tranform3 坐标变换对象
            * @remark 用于将世界空间的点或向量转换到局部空间。
            */
            Tranform3 WorldToLocal() const;

        public:
            /*
            * @brief 获取世界坐标系实例
            * @return const CoordSys3& 世界坐标系（单例）
            * @remark
            *   世界坐标系定义为：
            *   原点(0,0,0)，X=(1,0,0)，Y=(0,1,0)，Z=(0,0,1)
            */
            static const CoordSys3& WorldCoordSys();

        private:
            Axis3 m_Axis;     //!< X 轴（包含原点与方向）
            Dir3  m_dirY;     //!< Y 方向向量
            Dir3  m_dirZ;     //!< Z 方向向量
        };

        //------------比较运算符-------------

        /*
        * @brief 判断两个坐标系是否相等
        * @param [in] csLHS_ 左操作数
        * @param [in] csRHS_ 右操作数
        * @return true 坐标系相等 / false 不相等
        * @remark 比较原点位置与三个方向是否一致。
        */
        _MATH_EXP bool operator==(IN const CoordSys3& csLHS_, IN const CoordSys3& csRHS_);

        /*
        * @brief 坐标系小于比较（用于有序容器排序）
        * @param [in] csLHS_ 左操作数
        * @param [in] csRHS_ 右操作数
        * @return true 若 csLHS_ < r_，否则 false
        * @remark
        *   通常按原点坐标与方向向量字典序比较，
        *   用于 map / set 等容器中排序需求。
        */
        _MATH_EXP bool operator<(IN const CoordSys3& csLHS_, IN const CoordSys3& csRHS_);
    }
}
