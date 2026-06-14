#pragma once
#include "NcData.h"
#include "../Math/Point3.h"
#include "../Math/Vector3.h"
#include <vector>

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief 刀轴姿态链
        */
        struct _NCDATA_EXP ncPostrue final
        {
            /*
            * @brief 刀头姿态
            */
            struct _NCDATA_EXP Node final
            {
                Vector3 vDir;               //!< 刀头方向
                double nRatio = 0;              //!< 段号比
            };

        public:
            /*
            * @brief 构造函数
            */
            ncPostrue();

        public:
            /*
            * @brief 获取节点列表
            * @return std::vector<PostrueNode>&
            */
            std::vector<Node>& GetNodes();

            /*
            * @brief 获取节点列表
            * @return const std::vector<PostrueNode>&
            */
            const std::vector<Node>& GetNodes() const;

            /*
            * @brief 追加节点
            * @param [in] vDir_
            * @param [in] nRatio_
            */
            void PushNode(IN const Vector3& vDir_, IN const double& nRatio_);

        public:
            std::vector<Node> m_vecNodes;
        };
    }
}