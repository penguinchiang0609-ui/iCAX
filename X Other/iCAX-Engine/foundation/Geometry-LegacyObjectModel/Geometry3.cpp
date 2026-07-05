#include "pch.h"
#include "Geometry3.h"

//!< 构造函数
iCAX::Geom::Geometry3::Geometry3(IN const CoordSys3& csLocal_)
    : m_csLocal(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::Geometry3::~Geometry3()
{
}

//!< 局部坐标系
iCAX::Math::CoordSys3& iCAX::Geom::Geometry3::Local()
{
    return m_csLocal;
}

//!< 局部坐标系
const iCAX::Math::CoordSys3& iCAX::Geom::Geometry3::Local() const
{
    return m_csLocal;
}

//!< 仿射变换
void iCAX::Geom::Geometry3::Transform(IN const Tranform3& mat_)
{
    Tranform3 _matLocal = m_csLocal.WorldToLocal() * mat_ * m_csLocal.LocalToWorld();
    auto [_nTranslateLocal, _nRatateLocal, _nShearLocal, _nScaleLocal, _bMirrorX] = _matLocal.Decompose();

    ScaleAndShear(_nScaleLocal, _nShearLocal, _bMirrorX);

    //!< 坐标系变换
    TransAndRot(m_csLocal.LocalToWorld().Applied(Vector3(_nTranslateLocal)).XYZ(), _nRatateLocal);
}


//!< 执行几何变换
void iCAX::Geom::Geometry3::TransAndRot(IN const Double3& nTranslate_, IN const Quaternion& nRotate_)
{
    m_csLocal.Transform(nTranslate_, nRotate_);
}

