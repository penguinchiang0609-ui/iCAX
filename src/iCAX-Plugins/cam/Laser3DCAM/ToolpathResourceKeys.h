#pragma once

#include "Data/uuid.h"

#include <string>

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 根据刀路 EntityID 生成刀路曲线资源 key。
        */
        inline std::string MakePathCurveResourceID(IN const iCAX::Data::uuid& PathEntityID_)
        {
            return PathEntityID_.is_nil() ? std::string() : "path/" + iCAX::Data::to_string(PathEntityID_) + "#curve";
        }

        /*
        * @brief 根据刀路 EntityID 生成姿态场资源 key。
        */
        inline std::string MakePoseFieldResourceID(IN const iCAX::Data::uuid& PathEntityID_)
        {
            return PathEntityID_.is_nil() ? std::string() : "path/" + iCAX::Data::to_string(PathEntityID_) + "#pose-field";
        }
    }
}
