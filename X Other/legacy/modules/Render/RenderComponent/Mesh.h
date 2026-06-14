#pragma once
#include "Render.h"
#include "Data/uuid.h"
#include "Data/Array1.h"

using namespace iCAX::Data;

namespace iCAX
{
    namespace Render
    {
        /**
        * @enum cIndicesMode
        * @brief 网格索引模式
        * @details
        * 定义索引的渲染拓扑结构。
        * - kIndicesTriangles：三角形面片模式
        * - kIndicesLines：线段模式
        * - kIndicesPoints：点模式
        */
        enum _RENDERCOMPONENT_EXP cIndicesMode
        {
            kIndicesTriangles,   //!< 三角形面片渲染
            kIndicesLines,       //!< 线段渲染
            kIndicesPoints       //!< 点渲染
        };

        /**
        * @class rMesh
        * @brief 网格数据类
        * @details
        * rMesh 类存储几何数据（顶点、法向、UV、颜色、切线等），
        * 提供对数据的直接引用接口。
        * 不包含任何渲染逻辑，仅作数据容器。
        */
        struct _RENDERCOMPONENT_EXP rMesh final
        {
            std::vector<iCAX::Data::Real3>   vecVertices;   //!< 顶点坐标列表
            std::vector<iCAX::Data::Real3>  vecNormals;    //!< 法向量列表
            std::vector<iCAX::Data::Real4>  vecColors;     //!< 顶点颜色列表
            std::vector<int>      vecIndices;    //!< 索引列表
            std::vector<iCAX::Data::Real4>  vecTangents;   //!< 切向量列表
            std::vector<iCAX::Data::Real2>  vecUVs;        //!< UV 坐标列表
            cIndicesMode           nIndicesMode;  //!< 索引模式
            int nVersion = 0;//!< 版本号，初始为零。每次修改都需要加一，调用者需要注意
        };
    }
}
