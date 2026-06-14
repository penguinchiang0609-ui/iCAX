#pragma once
#include "PhysicsPrimative.h"
#include "../Math/CoordSys2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
        * @class cHalfPlane2
        * @brief 二维半平面（二维半空间）
        * @details
        *   由一条直线和法向量定义的二维空间区域。
        *   法向量指向半空间外侧，可用于物理查询、碰撞检测、空间分割等场景。
        *
        * @note 方法说明：
        *   - IsOn(pt)：点在直线上
        *   - IsFront(pt)：点在法向量指向的前方（半空间外侧）
        *   - IsBack(pt)：点在法向量指向的反向（半空间内侧）
        */
        class _PHYSICSPRIMATIVE_EXP cHalfPlane2
        {
        public:
            /**
            * @brief 默认构造函数
            * @details 直线通过原点，法向量为 (0,1)
            */
            cHalfPlane2();

            /**
            * @brief 构造函数
            * @param [in] ptOnLine 直线上的一点
            * @param [in] normal 法向量，指向半空间外侧
            */
            cHalfPlane2(IN const Point2& ptOnLine, IN const Dir2& normal);

        public:
            /**
            * @brief 获取直线上的一点
            * @return const Point2& 直线上的一点（只读）
            */
            const Point2& Location() const;

            /**
            * @brief 获取直线的法向量
            * @return const Dir2& 法向量，指向半空间外侧（只读）
            */
            const Dir2& Normal() const;

            /**
            * @brief 判断点是否在直线上
            * @param [in] pt 检测点
            * @return bool 点在直线上返回 true，否则 false
            */
            bool IsOn(IN const Point2& pt) const;

            /**
            * @brief 判断点是否在半平面前方（法向量指向方向）
            * @param [in] pt 检测点
            * @return bool 点在半平面前方返回 true，否则 false
            */
            bool IsFront(IN const Point2& pt) const;

            /**
            * @brief 判断点是否在半平面后方（法向量反方向）
            * @param [in] pt 检测点
            * @return bool 点在半平面后方返回 true，否则 false
            */
            bool IsBack(IN const Point2& pt) const;

        private:
            CoordSys2 m_csLocal; //!< 局部坐标系，Origin 为直线上一点，ZAxis 为法向量
        };
    }
}
