#pragma once
#include "NcData.h"
#include "Arc.h"
#include "Bezier.h"
#include "Circle.h"
#include "Ellipse.h"
#include "EllipseArc.h"
#include "Line.h"
#include "Polyline.h"
#include "Rectangle.h"
#include "RectangleRounded.h"
#include "Runway.h"
#include "../Math/Vector3.h"

namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief NC轨迹
        */
        struct _NCDATA_EXP NcTrace final
        {
        public:
            /*
            * @brief 构造函数
            */
            NcTrace();

            /*
            * @brief 拷贝构造函数
            * @param [in] Right_
            */
            NcTrace(IN const NcTrace& Right_);

            /*
            * @brief 赋值构造函数
            * @param [in] Right_
            */
            NcTrace& operator=(IN const NcTrace& Right_);

            /*
            * @brief 析构函数
            */
            ~NcTrace();

        public:
            /*
            * @brief 曲线类型
            */
            enum _NCDATA_EXP CurveType
            {
                kNcArc,
                kNcBezier3,
                kNcCircle,
                kNcEllipse,
                kNcEllipseArc,
                kNcLine,
                kNcRect,
                kNcRectRounded,
                kNcRunway,
                kNcPolyline
            } nType;
            union
            {
                ncArc Arc;
                ncBezier3 Bezier3;
                ncCircle Circle;
                ncEllipse Ellipse;
                ncEllipseArc EArc;
                ncLine Line;
                ncRectangle Rect;
                RectangleRounded RectRounded;
                ncRunway Runway;
                ncPolyline Polyline; 
            };
        };
    }
}