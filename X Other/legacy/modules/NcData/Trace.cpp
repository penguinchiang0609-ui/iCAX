#include "pch.h"
#include "Trace.h"

iCAX::NcData::NcTrace::NcTrace()
    : nType(kNcLine)
    , Line()
{
}

iCAX::NcData::NcTrace::NcTrace(IN const NcTrace& Right_)
    : nType(Right_.nType)
    , Line()
{
    switch (nType)
    {
    case iCAX::NcData::NcTrace::kNcArc:
        Arc = Right_.Arc;
        break;
    case iCAX::NcData::NcTrace::kNcBezier3:
        Bezier3 = Right_.Bezier3;
        break;
    case iCAX::NcData::NcTrace::kNcCircle:
        Circle = Right_.Circle;
        break;
    case iCAX::NcData::NcTrace::kNcEllipse:
        Ellipse = Right_.Ellipse;
        break;
    case iCAX::NcData::NcTrace::kNcEllipseArc:
        EArc = Right_.EArc;
        break;
    case iCAX::NcData::NcTrace::kNcLine:
        Line = Right_.Line;
        break;
    case iCAX::NcData::NcTrace::kNcRect:
        Rect = Right_.Rect;
        break;
    case iCAX::NcData::NcTrace::kNcRectRounded:
        RectRounded = Right_.RectRounded;
        break;
    case iCAX::NcData::NcTrace::kNcRunway:
        Runway = Right_.Runway;
        break;
    case iCAX::NcData::NcTrace::kNcPolyline:
        Polyline = Right_.Polyline;
        break;
    default:
        break;
    }
}

iCAX::NcData::NcTrace& iCAX::NcData::NcTrace::operator=(IN const NcTrace& Right_)
{
    switch (nType)
    {
    case iCAX::NcData::NcTrace::kNcArc:
        Arc = {};
        break;
    case iCAX::NcData::NcTrace::kNcBezier3:
        Bezier3 = {};
        break;
    case iCAX::NcData::NcTrace::kNcCircle:
        Circle = {};
        break;
    case iCAX::NcData::NcTrace::kNcEllipse:
        Ellipse = {};
        break;
    case iCAX::NcData::NcTrace::kNcEllipseArc:
        EArc = {};
        break;
    case iCAX::NcData::NcTrace::kNcLine:
        Line = {};
        break;
    case iCAX::NcData::NcTrace::kNcRect:
        Rect = {};
        break;
    case iCAX::NcData::NcTrace::kNcRectRounded:
        RectRounded = {};
        break;
    case iCAX::NcData::NcTrace::kNcRunway:
        Runway = {};
        break;
    case iCAX::NcData::NcTrace::kNcPolyline:
        Polyline = {};
        break;
    default:
        break;
    }

    nType = Right_.nType;

    switch (nType)
    {
    case iCAX::NcData::NcTrace::kNcArc:
        Arc = Right_.Arc;
        break;
    case iCAX::NcData::NcTrace::kNcBezier3:
        Bezier3 = Right_.Bezier3;
        break;
    case iCAX::NcData::NcTrace::kNcCircle:
        Circle = Right_.Circle;
        break;
    case iCAX::NcData::NcTrace::kNcEllipse:
        Ellipse = Right_.Ellipse;
        break;
    case iCAX::NcData::NcTrace::kNcEllipseArc:
        EArc = Right_.EArc;
        break;
    case iCAX::NcData::NcTrace::kNcLine:
        Line = Right_.Line;
        break;
    case iCAX::NcData::NcTrace::kNcRect:
        Rect = Right_.Rect;
        break;
    case iCAX::NcData::NcTrace::kNcRectRounded:
        RectRounded = Right_.RectRounded;
        break;
    case iCAX::NcData::NcTrace::kNcRunway:
        Runway = Right_.Runway;
        break;
    case iCAX::NcData::NcTrace::kNcPolyline:
        Polyline = Right_.Polyline;
        break;
    default:
        break;
    }

    return *this;
}

iCAX::NcData::NcTrace::~NcTrace()
{
    switch (nType)
    {
    case iCAX::NcData::NcTrace::kNcArc:
        Arc = {};
        break;
    case iCAX::NcData::NcTrace::kNcBezier3:
        Bezier3 = {};
        break;
    case iCAX::NcData::NcTrace::kNcCircle:
        Circle = {};
        break;
    case iCAX::NcData::NcTrace::kNcEllipse:
        Ellipse = {};
        break;
    case iCAX::NcData::NcTrace::kNcEllipseArc:
        EArc = {};
        break;
    case iCAX::NcData::NcTrace::kNcLine:
        Line = {};
        break;
    case iCAX::NcData::NcTrace::kNcRect:
        Rect = {};
        break;
    case iCAX::NcData::NcTrace::kNcRectRounded:
        RectRounded = {};
        break;
    case iCAX::NcData::NcTrace::kNcRunway:
        Runway = {};
        break;
    case iCAX::NcData::NcTrace::kNcPolyline:
        Polyline = {};
        break;
    default:
        break;
    }
}
