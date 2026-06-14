#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include "Worker.h"
#include "../../01 Foundation/Data/uuid.h"

namespace iCAX
{
    namespace WorkShop
    {
        /// <summary>
        /// 工种配置信息
        /// </summary>
        struct TradeSetting
        {
            iCAX::Data::uuid UID;                                         //!< 工种ID，唯一标记
            int nCount;                                                     //!< 该工种的工人人数
            std::function<std::shared_ptr<CWorkerBase>()> WorkerCreator;    //!< 构造工人的方法

            //!< 辅助说明
            std::string strName;                                            //!< 工种名称
            std::string strRemark;                                          //!< 工种说明
            std::string strCatalog;                                         //!< 工种类别
        };

        /// <summary>
        /// 车间配置信息
        /// </summary>
        struct PlantSetting final
        {
            std::list<TradeSetting> Trades;                                //!< 工种信息
        };
    }
}