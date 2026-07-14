#pragma once

#include "TransformComponent.h"

#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Database
    {
        class IEntity;
        class IRepository;
    }

    namespace Transform
    {
        _TRANSFORM_EXP uint32_t GetTransformContractVersion() noexcept;

        /*
        * @brief 设置 Transform 父级，并同步维护父节点的有序 ChildEntityIDs。
        * @param [in,out] Repository_ 当前场景仓储。
        * @param [in] ChildEntityID_ 要挂接的子 Entity。
        * @param [in] ParentEntityID_ 新父 Entity；传空 UUID 表示挂到世界根。
        * @param [out] strError_ 失败原因。
        * @return true 表示更新成功。
        */
        [[nodiscard]] _TRANSFORM_EXP bool SetTransformParent(
            IN iCAX::Database::IRepository& Repository_,
            IN const iCAX::Data::uuid& ChildEntityID_,
            IN const iCAX::Data::uuid& ParentEntityID_,
            OUT std::string& strError_);

        [[nodiscard]] _TRANSFORM_EXP bool SetTransformParent(
            IN iCAX::Database::IRepository& Repository_,
            IN const iCAX::Data::uuid& ChildEntityID_,
            IN const iCAX::Data::uuid& ParentEntityID_);

        /*
        * @brief 读取父节点下的有序 child EntityID。
        */
        [[nodiscard]] _TRANSFORM_EXP std::vector<iCAX::Data::uuid> GetTransformChildEntityIDs(
            IN const iCAX::Database::IRepository& Repository_,
            IN const iCAX::Data::uuid& ParentEntityID_);

        /*
        * @brief 按 ChildEntityIDs 顺序解析 child Entity。
        */
        [[nodiscard]] _TRANSFORM_EXP std::vector<std::shared_ptr<iCAX::Database::IEntity>> GetTransformChildren(
            IN const iCAX::Database::IRepository& Repository_,
            IN const iCAX::Data::uuid& ParentEntityID_);
    }
}
