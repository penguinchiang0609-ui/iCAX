#pragma once
#include "Header.h"
#include "SchemaBlock.h"
#include "DataBlock.h"
#include "ResourceBlock.h"


namespace iCAX
{
    namespace Document
    {
        /*
        * @brief 包
        */
        struct Package
        {
        //public:
        //    //! 头
        //    Header Header;
        //    //! 规约
        //    SchemaBlock Schema;
        //    //! 数据块
        //    DataBlock Component;
        //    //! 资源块
        //    ResourceBlock Resource;

        public:

            bool Open(const std::string& path);
            bool Save();
            bool SaveAs(const std::string& path);

            Entity CreateEntity();
            void DestroyEntity(Entity id);

            template<typename T>
            T* AddComponent(Entity id);

            template<typename T>
            T* GetComponent(Entity id);

            template<typename T>
            void RemoveComponent(Entity id);

            Repository* GetRepository();
            ResourceManager* GetResourceManager();
        };
    }
}