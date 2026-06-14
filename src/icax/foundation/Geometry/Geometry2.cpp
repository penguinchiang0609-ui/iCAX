#include "pch.h"
#include "Geometry2.h"

//!< 构造函数
iCAX::Geom::Geometry2::Geometry2(IN const CoordSys2& csLocal_)
    : m_csLocal(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::Geometry2::~Geometry2()
{
}

//!< 局部坐标系
iCAX::Math::CoordSys2& iCAX::Geom::Geometry2::Local()
{
    return m_csLocal;
}

//!< 局部坐标系
const iCAX::Math::CoordSys2& iCAX::Geom::Geometry2::Local() const
{
    return m_csLocal;
}

//!< 仿射变换
void iCAX::Geom::Geometry2::Transform(IN const Tranform2& mat_)
{
    Tranform2 _matLocal = m_csLocal.WorldToLocal() * mat_ * m_csLocal.LocalToWorld();
    auto [_nTranslateLocal, _nRatateLocal, _nShearLocal, _nScaleLocal, _bMirrorX] = _matLocal.Decompose();

    _nScaleLocal[1] = abs(_nScaleLocal[1]);
    ScaleAndShear(_nScaleLocal, _nShearLocal, _bMirrorX);

    //!< 坐标系变换
    TransAndRot(m_csLocal.LocalToWorld().Applied(Vector2(_nTranslateLocal)).XY(), _nRatateLocal);
}

//!< 执行几何变换
void iCAX::Geom::Geometry2::TransAndRot(IN const Double2& nTranslate_, IN const double& nRotate_)
{
    m_csLocal.Transform(nTranslate_, nRotate_);
}