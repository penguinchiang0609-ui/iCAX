#pragma once
#include "ChangeSet.h"

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IMetaRegistry;

        /*
        * @brief 过滤出可参与撤销还原的变更
        */
        CChangeSet FilterTransactionalChangeSet(IN const CChangeSet& ChangeSet_);
        CChangeSet FilterTransactionalChangeSet(IN const CChangeSet& ChangeSet_, IN const IMetaRegistry& Meta_);

        /*
        * @brief 过滤出可写入快速保存日志的变更
        */
        CChangeSet FilterPersistentChangeSet(IN const CChangeSet& ChangeSet_);
        CChangeSet FilterPersistentChangeSet(IN const CChangeSet& ChangeSet_, IN const IMetaRegistry& Meta_);

        /*
        * @brief 生成反向变更
        */
        CChangeSet MakeInverseChangeSet(IN const CChangeSet& ChangeSet_, IN const std::string& strName_ = std::string());

        /*
        * @brief ChangeSet 与 Variant 互转
        */
        iCAX::Data::Variant ChangeSetToVariant(IN const CChangeSet& ChangeSet_);
        CChangeSet ChangeSetFromVariant(IN const iCAX::Data::Variant& Value_);

        /*
        * @brief ChangeSet 字符串序列化
        */
        std::string SerializeChangeSet(IN const CChangeSet& ChangeSet_);
        CChangeSet DeserializeChangeSet(IN const std::string& Text_);

        /*
        * @brief 追加式操作日志
        */
        class CChangeSetJournal final
        {
        public:
            CChangeSetJournal() = default;
            ~CChangeSetJournal();

            CChangeSetJournal(IN const CChangeSetJournal&) = delete;
            CChangeSetJournal& operator=(IN const CChangeSetJournal&) = delete;

            void Open(IN const std::string& strPath_, IN const bool bTruncate_);
            void Close();
            bool IsOpen() const;
            const std::string& GetPath() const;

            void Append(IN const CChangeSet& ChangeSet_);
            std::vector<CChangeSet> ReadAll(IN const std::string& strPath_) const;

        private:
            std::string m_strPath;
            std::ofstream m_Stream;
        };
    }
}
