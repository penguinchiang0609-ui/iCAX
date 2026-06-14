#pragma once
#include "PhysicsPrimative.h"
#include "../Math/CoordSys3.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Physics
    {
        /**
         * @class cHalfPlane3
         * @brief 三维半空间
         * @details
         *   由平面定义的三维空间区域，平面由一个点和法向量确定。
         *   法向量指向半空间外侧，可用于物理查询、碰撞检测、空间分割等场景。
         *
         * @note 方法说明：
         *   - IsOn(pt)：点在平面上
         *   - IsFront(pt)：点在平面法向量指向的前方（半空间外侧）
         *   - IsBack(pt)：点在平面法向量指向的反向（半空间内侧）
         */
        class _PHYSICSPRIMATIVE_EXP cHalfPlane3
        {
        public:
            /**
             * @brief 默认构造函数
             * @details 平面通过原点，法向量为 (0,0,1)
             */
            cHalfPlane3();

            /**
             * @brief 构造函数
             * @param [in] ptOnPlane 平面上的一点
             * @param [in] normal 法向量，指向半空间外侧
             */
            cHalfPlane3(IN const Point3& ptOnPlane, IN const Dir3& normal);

        public:
            /**
             * @brief 获取平面上的一点
             * @return const Point3& 平面上一点（只读）
             */
            const Point3& Location() const;

            /**
             * @brief 获取平面的法向量
             * @return const Dir3& 法向量，指向半空间外侧（只读）
             */
            const Dir3& Normal() const;

            /**
             * @brief 判断点是否在平面上
             * @param [in] pt 检测点
             * @return bool 点在平面上返回 true，否则 false
             */
            bool IsOn(IN const Point3& pt) const;

            /**
             * @brief 判断点是否在平面前方（法向量指向方向）
             * @param [in] pt 检测点
             * @return bool 点在半空间前方返回 true，否则 false
             */
            bool IsFront(IN const Point3& pt) const;

            /**
             * @brief 判断点是否在平面后方（法向量反方向）
             * @param [in] pt 检测点
             * @return bool 点在半空间后方返回 true，否则 false
             */
            bool IsBack(IN const Point3& pt) const;

        private:
            CoordSys3 m_csLocal; //!< 局部坐标系，Origin 为平面点，ZAxis 为法向量
        };
    }
}
