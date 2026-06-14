#pragma once
#include "Math.h"
#include "CoordSys3.h"
#include "Point2.h"

namespace iCAX
{
    namespace Math
    {
        /*
        * @brief 三维平面
        */
        class _MATH_EXP Plane3 final
        {
        public:
            /*
            * @brief 使用坐标系构造
            * @param [in] csLocal_ 距离
            */
            Plane3(IN const CoordSys3& csLocal_);

            /*
            * @brief 使用距离世界原点的距离与法向构造
            * @param [in] nDistance_ 距离
            * @param [in] dirNormal_ 法向
            * @remark
            *   Z 方向通过 X、Y 方向向量的叉乘推导而得。
            *   用于定义任意旋转或倾斜的局部坐标系。
            */
            Plane3(IN const double& nDistance_, IN const Dir3 dirNormal_);

            /*
            * @brief 使用距离世界原点的距离与法向构造
            * @param [in] ptCenter_ 坐标
            * @param [in] dirNormal_ 法向
            * @remark
            *   Z 方向通过 X、Y 方向向量的叉乘推导而得。
            *   用于定义任意旋转或倾斜的局部坐标系。
            */
            Plane3(IN const Point3& ptCenter_, IN const Dir3 dirNormal_);

            /*
            * @brief 三点构造平面，右手螺旋法向
            * @param [in] pt0_
            * @param [in] pt1_
            * @param [in] pt2_
            * @remark
            */
            Plane3(IN const Point3& pt0_, IN const Point3& pt1_, IN const Point3& pt2_);

        public://!< 成员访问
            /*
            * @brief 获取本地坐标系
            * @return CoordSys3&
            */
            CoordSys3& Local();

            /*
            * @brief 获取本地坐标系
            * @return  const CoordSys3&
            */
            const CoordSys3& Local() const;

            /*
            * @brief 获取位置
            * @return Point3
            */
            Point3 Location() const;

            /*
            * @brief 法向
            * @return Dir3
            */
            Dir3 Normal() const;

        public://!< 几何算法
            /*
            * @brief 点是否位于平面上
            * @param [in] ptTarget_
            * @return bool
            */
            bool IsON(IN const Point3& ptTarget_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 判断是否平行
            * @param [in] dirOther_ 另一方向
            * @param [in] nTol_ 公差（默认 EPSILON）
            * @return bool 若两方向平行则返回 true
            */
            bool IsParallel(IN const Dir3& dirNormal_, IN const double& nTol_ = EPSILON) const;

            /*
            * @brief 点到平面的距离
            * @param [in] ptTarget_
            * @return double
            */
            double Distance(IN const Point3& ptTarget_) const;

            /*
            * @brief 点投影到平面
            * @param [in] ptTarget_
            * @return Point3
            */
            Point3 Project(IN const Point3& ptTarget_) const;

            /*
            * @brief 投影到平面内的二维坐标系
            * @param [in] ptTarget_
            * @return Point2
            */
            Point2 Project2Plane(IN const Point3& ptTarget_) const;

            /*
            * @brief 投影到三维空间
            * @param [in] ptTarget_
            * @return Point3
            */
            Point3 Project2Space(IN const Point2& ptTarget_) const;

            /*
            * @brief 是否等价
            * @param [in] pln_
            * @return boo
            * @details
            *   平面重合
            *   法向一致或相反
            */
            bool IsEqual(IN const Plane3& pln_);

        private:
            CoordSys3 m_csLocal;
        };
    }
}