#pragma once

#include "RenderServiceExport.h"
#include "RenderData/RenderData.h"

#include <unordered_map>
#include <vector>

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 一个渲染场景的只读快照。
        * @details
        *   Snapshot 是 RenderService 和具体渲染实现之间的数据交接结构。它按 scene 隔离，
        *   不包含 ProjectContext 指针、PDOHub 指针或具体渲染器状态。
        */
        struct _RENDER_SERVICE_EXP SRenderSceneSnapshot final
        {
            RenderSceneID nSceneID = kInvalidRenderSceneID;
            std::unordered_map<RenderGeometryID, SRenderMeshData> Meshes;
            std::unordered_map<RenderGeometryID, SRenderPolylineData> Polylines;
            std::unordered_map<RenderGeometryID, SRenderToolpathData> Toolpaths;
            std::vector<SRenderTransformData> Transforms;
            std::vector<SRenderInstanceData> Instances;
            std::vector<SRenderStyleData> Styles;
            std::vector<SRenderCameraData> Cameras;
            RenderCameraID nActiveCameraID = kInvalidRenderCameraID;
            RenderDataVersion nTransformDataVersion = 0;
            RenderDataVersion nInstanceDataVersion = 0;
            RenderDataVersion nCameraDataVersion = 0;
        };
    }
}
