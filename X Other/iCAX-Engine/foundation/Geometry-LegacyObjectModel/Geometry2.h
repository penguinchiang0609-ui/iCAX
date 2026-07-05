#pragma once
#include "GeometryPrimative.h"
#include "Geometry.h"
#include "../Math/Tranform2.h"
#include "../Math/CoordSys2.h"

using namespace iCAX::Math;

namespace iCAX
{
    namespace Geom
    {
        /*
        * @brief 二维几何元素
        */
        class _GEOMETRY_EXP Geometry2 : public Geometry
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] csLocal_ 自身坐标系
            */
            Geometry2(IN const CoordSys2& csLocal_);

            /*
            * @brief 析构函数
            */
            virtual ~Geometry2();

        public:
            /*
            * @brief 自身坐标系
            * @return CoordSys2
            */
            CoordSys2& Local();

            /*
            * @brief 自身坐标系
            * @return const CoordSys2&
            */
            const CoordSys2& Local() const;

        public:
            /**
            * @brief 执行几何变换
            * @param [in] mat_ 二维仿射变换（旋转、平移、缩放）
            */
            void Transform(IN const Tranform2& mat_);

            /**
            * @brief 平移和旋转
            * @param [in] nTranslate_
            * @param [in] nRotate_
            */
            void TransAndRot(IN const Double2& nTranslate_, IN const double& nRotate_);

            /**
            * @brief 缩放和剪切
            * @param [in] nScale_
            * @param [in] nShear_
            * @param [in] bMirrorX_ 是否沿X轴镜像
            */
            virtual void ScaleAndShear(IN const Double2& nScale_, IN const Double2& nShear_, IN const bool& bMirrorX_) = 0;

        protected:
            CoordSys2 m_csLocal;            //!< 自身坐标系
        };
    }
}