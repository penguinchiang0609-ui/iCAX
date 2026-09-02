#pragma once

#include "OpenCascadeResourceImportExport.h"

#include "GeometryData/GeometryData.h"

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace iCAX::OpenCascade
{
    struct _OPEN_CASCADE_RESOURCE_IMPORT_EXP SBRepFileReadResult final
    {
        bool bOK = false;
        iCAX::GeometryData::BRepModel Geometry;
        std::string FormatID;
        std::string DisplayName;
        std::string ContentHash;
        std::vector<std::string> Diagnostics;
    };

    /*
    * @brief 把 CAD 文件解码成内存 BRep，不向任何资源库写入数据。
    * @details 供“解码 -> 识别/规范化 -> 成功后发布资源”的事务式导入流程使用。
    */
    _OPEN_CASCADE_RESOURCE_IMPORT_EXP SBRepFileReadResult ReadBRepFile(
        IN const std::string& strSourcePath_,
        IN double dTolerance_ = 0.001);

    _OPEN_CASCADE_RESOURCE_IMPORT_EXP iCAX::GeometryData::BRepModel ConvertOpenCascadeShapeToBRep(
        IN const TopoDS_Shape& Shape_,
        IN const std::string& strDisplayName_,
        IN const std::string& strSourceID_,
        IN double dTolerance_ = 0.001);
}
