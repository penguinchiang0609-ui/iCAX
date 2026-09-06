#pragma once

#include "ApplicationContextExport.h"
#include "Data/Variant.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 一条记录到另一个稳定对象的显式关系。
        * @details TargetKind 只能是 definition、user-record 或 external。
        */
        struct _APPLICATION_CONTEXT_EXP CUserDataLink final
        {
            std::string RelationType;
            std::string TargetKind;
            std::string TargetFeatureID;
            std::string TargetType;
            std::string TargetID;
        };

        /*
        * @brief 一条跨项目、用户作用域的业务记录。
        * @details
        *   ProductID + FeatureID + RecordType + RecordID 组成稳定主键。
        *   SubjectType + SubjectID 表达主要作用对象，Links 表达附加关系。
        */
        struct _APPLICATION_CONTEXT_EXP CUserDataRecord final
        {
            std::string ProductID;
            std::string FeatureID;
            std::string RecordType;
            std::string SubjectType;
            std::string SubjectID;
            std::string RecordID;
            std::string OwnerScope = "personal";
            std::uint32_t SchemaVersion = 1;
            iCAX::Data::Variant Payload;
            std::vector<CUserDataLink> Links;
            std::uint64_t Revision = 0;
            std::string CreatedAt;
            std::string UpdatedAt;
            std::string DeletedAt;
        };

        struct _APPLICATION_CONTEXT_EXP CUserDataQuery final
        {
            std::string ProductID;
            std::string FeatureID;
            std::string RecordType;
            std::string SubjectType;
            std::string SubjectID;
            std::string OwnerScope;
            bool IncludeDeleted = false;
        };

        /*
        * @brief 跨项目的 user-scope 数据仓库。
        * @details
        *   ExpectedRevision 为空表示覆盖写，0 表示只允许新建，其他值表示乐观并发更新。
        *   device、project 和 session 数据由各自存储承载，不进入本接口。
        */
        class _APPLICATION_CONTEXT_EXP IUserDataStore
        {
        public:
            IUserDataStore() = default;
            virtual ~IUserDataStore() = default;

            IUserDataStore(IN const IUserDataStore&) = delete;
            IUserDataStore& operator=(IN const IUserDataStore&) = delete;

        public:
            virtual std::vector<CUserDataRecord> List(IN const CUserDataQuery& Query_) = 0;
            virtual std::optional<CUserDataRecord> Get(
                IN const std::string& strProductID_,
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN bool bIncludeDeleted_ = false) = 0;
            virtual CUserDataRecord Put(
                IN const CUserDataRecord& Record_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) = 0;
            virtual bool Delete(
                IN const std::string& strProductID_,
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) = 0;
        };

        class _APPLICATION_CONTEXT_EXP CSqliteUserDataStore final : public IUserDataStore
        {
        public:
            explicit CSqliteUserDataStore(IN std::string strDatabasePath_);
            ~CSqliteUserDataStore() override;

            CSqliteUserDataStore(IN const CSqliteUserDataStore&) = delete;
            CSqliteUserDataStore& operator=(IN const CSqliteUserDataStore&) = delete;

        public:
            std::vector<CUserDataRecord> List(IN const CUserDataQuery& Query_) override;
            std::optional<CUserDataRecord> Get(
                IN const std::string& strProductID_,
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN bool bIncludeDeleted_ = false) override;
            CUserDataRecord Put(
                IN const CUserDataRecord& Record_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) override;
            bool Delete(
                IN const std::string& strProductID_,
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) override;

            const std::string& GetDatabasePath() const noexcept;

        private:
            struct CImpl;
            std::unique_ptr<CImpl> m_pImpl;
        };

        struct _APPLICATION_CONTEXT_EXP CUserDataRelationDescriptor final
        {
            std::string RelationType;
            std::string TargetKind;
            std::string TargetFeatureID;
            std::string TargetType;
        };

        struct _APPLICATION_CONTEXT_EXP CUserDataRecordTypeDescriptor final
        {
            std::string RecordType;
            std::vector<std::string> SubjectTypes;
            std::uint32_t SchemaVersion = 1;
            bool AllowMultiple = true;
            std::vector<CUserDataRelationDescriptor> Relations;
        };

        struct _APPLICATION_CONTEXT_EXP CUserDataFeatureDescriptor final
        {
            std::string FeatureID;
            std::vector<CUserDataRecordTypeDescriptor> RecordTypes;
        };

        /*
        * @brief 产品可写的用户记录。ProductID 和 StorageScope 不可写。
        * @details 该接口固定写入当前产品的 user scope。
        */
        struct _APPLICATION_CONTEXT_EXP CProductUserDataRecord final
        {
            std::string FeatureID;
            std::string RecordType;
            std::string SubjectType;
            std::string SubjectID;
            std::string RecordID;
            std::string OwnerScope = "personal";
            std::uint32_t SchemaVersion = 1;
            iCAX::Data::Variant Payload;
            std::vector<CUserDataLink> Links;
            std::uint64_t Revision = 0;
            std::string CreatedAt;
            std::string UpdatedAt;
            std::string DeletedAt;
        };

        struct _APPLICATION_CONTEXT_EXP CProductUserDataQuery final
        {
            std::string FeatureID;
            std::string RecordType;
            std::string SubjectType;
            std::string SubjectID;
            std::string OwnerScope;
            bool IncludeDeleted = false;
        };

        /*
        * @brief 已锁定 ProductID 且受 manifest 描述约束的用户数据仓库。
        */
        class _APPLICATION_CONTEXT_EXP IProductUserDataStore
        {
        public:
            IProductUserDataStore() = default;
            virtual ~IProductUserDataStore() = default;

            IProductUserDataStore(IN const IProductUserDataStore&) = delete;
            IProductUserDataStore& operator=(IN const IProductUserDataStore&) = delete;

        public:
            virtual const std::string& GetProductID() const noexcept = 0;
            virtual std::vector<CProductUserDataRecord> List(
                IN const CProductUserDataQuery& Query_) = 0;
            virtual std::optional<CProductUserDataRecord> Get(
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN bool bIncludeDeleted_ = false) = 0;
            virtual CProductUserDataRecord Put(
                IN const CProductUserDataRecord& Record_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) = 0;
            virtual bool Delete(
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) = 0;
        };

        class _APPLICATION_CONTEXT_EXP CProductUserDataStore final : public IProductUserDataStore
        {
        public:
            CProductUserDataStore(
                IN std::shared_ptr<IUserDataStore> pStore_,
                IN std::string strProductID_,
                IN std::vector<CUserDataFeatureDescriptor> Descriptors_ = {});
            ~CProductUserDataStore() override;

            CProductUserDataStore(IN const CProductUserDataStore&) = delete;
            CProductUserDataStore& operator=(IN const CProductUserDataStore&) = delete;

        public:
            const std::string& GetProductID() const noexcept override;
            std::vector<CProductUserDataRecord> List(
                IN const CProductUserDataQuery& Query_) override;
            std::optional<CProductUserDataRecord> Get(
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN bool bIncludeDeleted_ = false) override;
            CProductUserDataRecord Put(
                IN const CProductUserDataRecord& Record_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) override;
            bool Delete(
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_,
                IN const std::string& strRecordID_,
                IN std::optional<std::uint64_t> ExpectedRevision_ = std::nullopt) override;

        private:
            const CUserDataRecordTypeDescriptor& RequireRecordType(
                IN const std::string& strFeatureID_,
                IN const std::string& strRecordType_) const;
            void ValidateRecord(IN const CProductUserDataRecord& Record_) const;

        private:
            std::shared_ptr<IUserDataStore> m_pStore;
            std::string m_strProductID;
            std::vector<CUserDataFeatureDescriptor> m_Descriptors;
        };
    }
}
