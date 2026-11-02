#include "pch.h"
#include "EditableToolpathHostEdit.h"
#include "EditableToolpathInternal.h"
#include "Database/IRepository.h"
#include <exception>

namespace iCAX::CAM::Intent::Editable
{
void ValidateHostSourceIdentities(const std::vector<IntentMap>& commands,
    const SEditableToolpathHostEdit& host)
{
    Require(!host.UndoName.empty(), "TOOLPATH_HOST_UNDO_NAME_REQUIRED");
    std::map<IntentId, const IntentMap*> byId;
    for (const auto& command : commands)
    {
        const auto id = ID(At(command, "commandId"));
        Require(!id.is_nil(), "TOOLPATH_COMMAND_ID_REQUIRED");
        Require(byId.emplace(id, &command).second, "TOOLPATH_BATCH_COMMAND_ID_DUPLICATE");
    }
    std::set<IntentId> claimed,operations;
    for (const auto& [commandId, identity] : host.SourceIdentities)
    {
        const auto command = byId.find(commandId);
        Require(command != byId.end() && Text(*command->second, "kind") == "CreateSource",
            "TOOLPATH_HOST_IDENTITY_REQUIRES_SOURCE_COMMAND");
        if(!identity.SourceOperationId.is_nil())Require(operations.insert(identity.SourceOperationId).second,"TOOLPATH_HOST_SOURCE_OPERATION_DUPLICATE");
        switch (identity.Mode)
        {
        case ESourcePathIdentityMode::AllocateNew:
            Require(identity.PathEntityId.is_nil(), "TOOLPATH_HOST_ALLOCATED_ID_MUST_BE_EMPTY");
            break;
        case ESourcePathIdentityMode::CreateWithId:
        case ESourcePathIdentityMode::AdoptExisting:
            Require(!identity.PathEntityId.is_nil(), "TOOLPATH_HOST_PATH_ID_REQUIRED");
            Require(claimed.insert(identity.PathEntityId).second, "TOOLPATH_HOST_PATH_ID_DUPLICATE");
            break;
        default:
            throw std::invalid_argument("TOOLPATH_HOST_IDENTITY_MODE_INVALID");
        }
    }
    for(const auto& operation:operations)Require(!claimed.contains(operation),"TOOLPATH_HOST_IDENTITY_ROLE_COLLISION");
    for(const auto& [commandId,transform]:host.PlanClones)
    {const auto command=byId.find(commandId);Require(command!=byId.end()&&Text(*command->second,"kind")=="ClonePlan","TOOLPATH_HOST_CLONE_REQUIRES_CLONE_COMMAND");}
}

void CommitToolpathHostEdit(iCAX::Database::IRepository& repository, IntentId document,
    const SEditableToolpathHostEdit& host, const std::vector<IntentMap>& receipts,
    const std::vector<IntentId>& retainedReceipts,
    const std::function<void(iCAX::Database::ITransaction&)>& stageDomain)
{
    Require(!host.UndoName.empty(), "TOOLPATH_HOST_UNDO_NAME_REQUIRED");
    Require(static_cast<bool>(stageDomain), "TOOLPATH_HOST_DOMAIN_STAGE_REQUIRED");
    if (receipts.empty()) return;
    Require(host.RecordUndo||!repository.IsUndoCommandRecording(),"TOOLPATH_NON_UNDO_EDIT_DURING_UNDO_COMMAND");
    if (host.Preflight) host.Preflight(receipts);
    auto undo = repository.BeginUndoCommand(host.UndoName);
    iCAX::Database::ITransaction* transaction = nullptr;
    bool committing = false;
    try
    {
        transaction = &repository.BeginTransaction(host.UndoName);
        stageDomain(*transaction);
        if (host.Stage) host.Stage(*transaction, receipts);
        committing = true; // Commit consumes the transaction on both outcomes.
        std::string error;
        if (!repository.CommitTransaction(*transaction, error))
            throw std::runtime_error("TOOLPATH_TRANSACTION_FAILED: " + error);
        // Validate the whole retention set before changing history policy for
        // any receipt. Product staging cannot turn a durable acknowledgement
        // into a mixed product entity or move it under another owner.
        for (const auto& receipt : retainedReceipts)
        {
            const auto entity = repository.GetEntity(receipt);
            Require(entity && !entity->GetComponentClasses().empty(), "TOOLPATH_HOST_RECEIPT_MISSING");
            for (const auto& type : entity->GetComponentClasses())
            {
                const auto component = std::dynamic_pointer_cast<iCAX::Database::CDerivedComponentBase>(entity->GetComponent(type));
                Require(component && component->GetDerivedOwnerID() == document, "TOOLPATH_HOST_RECEIPT_OWNERSHIP_INVALID");
            }
        }
        if(host.RecordUndo)
        {for (const auto& receipt : retainedReceipts)repository.RetainDerivedEntityForUndo(receipt, document);undo->End();}
        else undo->EndWithoutHistory();
    }
    catch (...)
    {
        const auto failure = std::current_exception();
        if (transaction && !committing) repository.CancelTransaction(*transaction);
        if(undo)undo->Cancel();
        std::rethrow_exception(failure);
    }
}
}
