TEST(DatabaseAutoSaveCheckpointTest, DetectsPendingWritesAndFlushesActualNumericUndoRedoCancelJournal)
{
    const auto path=MakeTempOperationLogPath();auto db=GenerateTestRepository();EXPECT_FALSE(db->FlushOperationLog());EXPECT_FALSE(db->IsWriteInProgress());db->OpenOperationLog(path,true,S_TestOperationLogMagic,S_TestOperationLogVersion);auto component=db->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();const auto entity=component->GetEntity()->GetID();
    {auto scope=db->BeginUndoCommand("Numeric gesture");EXPECT_TRUE(db->IsWriteInProgress());ASSERT_TRUE(component->SetA(17));scope->End();}EXPECT_FALSE(db->IsWriteInProgress());ASSERT_TRUE(db->FlushOperationLog());ASSERT_TRUE(db->Undo());ASSERT_TRUE(db->FlushOperationLog());ASSERT_TRUE(db->Redo());{auto scope=db->BeginUndoCommand("Cancelled numeric gesture");ASSERT_TRUE(component->SetA(91));scope->Cancel();}EXPECT_EQ(17,component->GetA());ASSERT_TRUE(db->FlushOperationLog());
    auto& tx=db->BeginTransaction("Pending numeric transaction");EXPECT_TRUE(db->IsWriteInProgress());db->CancelTransaction(tx);EXPECT_FALSE(db->IsWriteInProgress());db->CloseOperationLog();EXPECT_FALSE(db->FlushOperationLog());auto restored=GenerateTestRepository();restored->ReplayOperationLog(path,S_TestOperationLogMagic,S_TestOperationLogVersion);const auto value=std::dynamic_pointer_cast<CSumComponent>(restored->GetEntity(entity)->GetComponent("CSumComponent"));ASSERT_TRUE(value);EXPECT_EQ(17,value->GetA());std::filesystem::remove(path);
}
TEST(DatabaseAutoSaveCheckpointTest, ARealWindowsWriteLockFailureCannotBeAcknowledgedByALaterFlush)
{
    const auto path=MakeTempOperationLogPath();COperationBatchJournal journal;journal.Open(path,true,S_TestOperationLogMagic,S_TestOperationLogVersion);ASSERT_TRUE(journal.Flush());const auto wide=std::filesystem::path(path).wstring();const HANDLE blocker=CreateFileW(wide.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);ASSERT_NE(INVALID_HANDLE_VALUE,blocker);OVERLAPPED overlap{};
    const bool locked=LockFileEx(blocker,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&overlap)!=0;if(!locked){CloseHandle(blocker);FAIL()<<"Could not obtain the real file write lock";}
    COperationBatch batch;batch.Name="Actual failed journal append";CRepositoryOperation operation;operation.Type=RepositoryEventArgs::kAddEntity;operation.EntityID=GenerateNewUUID();batch.Operations.push_back(operation);
    EXPECT_THROW(journal.Append(batch),std::runtime_error);
    EXPECT_THROW(journal.Flush(),std::runtime_error);
    UnlockFileEx(blocker,0,MAXDWORD,MAXDWORD,&overlap);CloseHandle(blocker);
    EXPECT_THROW(journal.Flush(),std::runtime_error);
    journal.Close();journal.Open(path,true,S_TestOperationLogMagic,S_TestOperationLogVersion);
    EXPECT_NO_THROW(journal.Append(batch));
    EXPECT_TRUE(journal.Flush());journal.Close();EXPECT_EQ(1u,journal.ReadAll(path,S_TestOperationLogMagic,S_TestOperationLogVersion).size());std::filesystem::remove(path);
}

TEST(DatabaseAutoSaveCheckpointTest, RejectingCheckpointPreservesDirectMutationUndoRedoHistoryAndJournal)
{
    const auto path = MakeTempOperationLogPath(); auto db = GenerateTestRepository();
    db->OpenOperationLog(path, true, S_TestOperationLogMagic, S_TestOperationLogVersion);
    auto component = db->CreateEntity(GenerateNewUUID())->AddComponent<CSumComponent>();
    { auto scope = db->BeginUndoCommand("Checkpoint protected value"); ASSERT_TRUE(component->SetA(17)); scope->End(); }
    ASSERT_TRUE(db->FlushOperationLog());
    COperationBatchJournal reader;
    const auto count = reader.ReadAll(path, S_TestOperationLogMagic, S_TestOperationLogVersion).size();
    bool reject = true;
    db->SetOperationLogCheckpoint([&] { if (reject) throw std::runtime_error("Real owner checkpoint rejected write"); });
    EXPECT_THROW(component->SetA(91), std::runtime_error); EXPECT_EQ(17, component->GetA());
    EXPECT_THROW(db->Undo(), std::runtime_error); EXPECT_EQ(17, component->GetA()); EXPECT_TRUE(db->CanUndo()); EXPECT_FALSE(db->CanRedo());
    ASSERT_TRUE(db->FlushOperationLog()); EXPECT_EQ(count, reader.ReadAll(path, S_TestOperationLogMagic, S_TestOperationLogVersion).size());
    reject = false; ASSERT_TRUE(db->Undo()); EXPECT_EQ(0, component->GetA());
    ASSERT_TRUE(db->FlushOperationLog()); const auto undoCount = reader.ReadAll(path, S_TestOperationLogMagic, S_TestOperationLogVersion).size();
    reject = true; EXPECT_THROW(db->Redo(), std::runtime_error); EXPECT_EQ(0, component->GetA()); EXPECT_TRUE(db->CanRedo());
    ASSERT_TRUE(db->FlushOperationLog()); EXPECT_EQ(undoCount, reader.ReadAll(path, S_TestOperationLogMagic, S_TestOperationLogVersion).size());
    reject = false; EXPECT_TRUE(db->Redo()); EXPECT_EQ(17, component->GetA());
    db->SetOperationLogCheckpoint({}); db->CloseOperationLog(); std::filesystem::remove(path);
}
