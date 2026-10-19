#include "PracticeTools.h"

IMod *BMLEntry(IBML *bml) {
    return new PracticeTools(bml);
}

void BMLExit(IMod *mod) {
    delete mod;
}

PracticeTools::~PracticeTools() {
    ClearState();
}

void PracticeTools::OnLoad() {
    m_Context = m_BML->GetCKContext();
    m_Time = m_BML->GetTimeManager();
    m_Input = m_Context ? static_cast<CKInputManager *>(
        m_Context->GetManagerByGuid(INPUT_MANAGER_GUID)) : nullptr;

    IConfig *config = GetConfig();
    config->SetCategoryComment("Keys", "Practice controls. Each action uses one independently configurable key.");

    m_FullSpeedProperty = config->GetProperty("Keys", "FullSpeed");
    m_FullSpeedProperty->SetDefaultKey(CKKEY_TAB);
    m_FullSpeedProperty->SetComment("Hold to temporarily ignore the practice frame cap and run at 264 samples/s.");

    m_SaveProperty = config->GetProperty("Keys", "SaveState");
    m_SaveProperty->SetDefaultKey(CKKEY_F5);
    m_SaveProperty->SetComment("Capture the current level state in memory.");

    m_RestoreProperty = config->GetProperty("Keys", "RestoreState");
    m_RestoreProperty->SetDefaultKey(CKKEY_F8);
    m_RestoreProperty->SetComment("Restore the most recently captured state.");
    RefreshSettings();
    GetLogger()->Info("Practice controls initialized: full-speed=%d save=%d restore=%d input=%p.",
                      static_cast<int>(m_FullSpeedKey), static_cast<int>(m_SaveKey),
                      static_cast<int>(m_RestoreKey), static_cast<void *>(m_Input));
}

void PracticeTools::OnUnload() {
    EndFullSpeed();
    ClearState();
}

void PracticeTools::OnModifyConfig(const char *, const char *, IProperty *) {
    RefreshSettings();
}

void PracticeTools::RefreshSettings() {
    if (m_FullSpeedProperty)
        m_FullSpeedKey = m_FullSpeedProperty->GetKey();
    if (m_SaveProperty)
        m_SaveKey = m_SaveProperty->GetKey();
    if (m_RestoreProperty)
        m_RestoreKey = m_RestoreProperty->GetKey();
}

void PracticeTools::OnProcess() {
    if (!m_Input) {
        m_Input = m_Context ? static_cast<CKInputManager *>(
            m_Context->GetManagerByGuid(INPUT_MANAGER_GUID)) : nullptr;
        if (m_Input)
            GetLogger()->Info("Practice controls acquired the input manager.");
    }
    if (!m_Time)
        m_Time = m_BML->GetTimeManager();
    if (!m_Context)
        m_Context = m_BML->GetCKContext();
    if (!m_Input || !m_Time)
        return;

    const bool playing = m_BML->IsPlaying();
    const bool fullSpeedDown = playing && m_FullSpeedKey &&
                               m_Input->IsKeyDown(m_FullSpeedKey);
    const bool saveDown = m_SaveKey && m_Input->IsKeyDown(m_SaveKey);
    const bool restoreDown = m_RestoreKey && m_Input->IsKeyDown(m_RestoreKey);
    if (fullSpeedDown && !m_FullSpeedActive)
        BeginFullSpeed();
    else if (!fullSpeedDown && m_FullSpeedActive)
        EndFullSpeed();

    if (playing && saveDown && !m_SaveDown)
        SaveState();
    else if (playing && restoreDown && !m_RestoreDown)
        RestoreState();
    m_SaveDown = saveDown;
    m_RestoreDown = restoreDown;
}

void PracticeTools::BeginFullSpeed() {
    if (m_FullSpeedActive || !m_Time)
        return;
    m_PreviousFrameMode = static_cast<CK_FRAMERATE_LIMITS>(
        m_Time->GetLimitOptions() & CK_FRAMERATE_MASK);
    m_PreviousFrameLimit = m_Time->GetFrameRateLimit();
    m_Time->ChangeLimitOptions(CK_FRAMERATE_FREE);
    m_FullSpeedActive = true;
    GetLogger()->Info("Full-speed override enabled (264 samples/s).");
}

void PracticeTools::EndFullSpeed() {
    if (!m_FullSpeedActive || !m_Time)
        return;
    if (m_PreviousFrameMode == CK_FRAMERATE_LIMIT && m_PreviousFrameLimit > 0.0f)
        m_Time->SetFrameRateLimit(m_PreviousFrameLimit);
    m_Time->ChangeLimitOptions(m_PreviousFrameMode);
    m_FullSpeedActive = false;
    GetLogger()->Info("Full-speed override released; restored mode %d at %.3f Hz.",
                      static_cast<int>(m_PreviousFrameMode), m_PreviousFrameLimit);
}

bool PracticeTools::IsSnapshotObject(CKObject *object) {
    if (!object || object->IsToBeDeleted())
        return false;
    const CK_CLASSID cid = object->GetClassID();
    return CKIsChildClassOf(cid, CKCID_PARAMETER) ||
           CKIsChildClassOf(cid, CKCID_BEHAVIOR) ||
           CKIsChildClassOf(cid, CKCID_DATAARRAY) ||
           CKIsChildClassOf(cid, CKCID_3DENTITY) ||
           CKIsChildClassOf(cid, CKCID_OBJECTANIMATION) ||
           CKIsChildClassOf(cid, CKCID_WAVESOUND) ||
           CKIsChildClassOf(cid, CKCID_MIDISOUND) ||
           cid == CKCID_BEHAVIORIO || cid == CKCID_BEHAVIORLINK ||
           cid == CKCID_PARAMETEROPERATION || cid == CKCID_SYNCHRO ||
           cid == CKCID_STATE;
}

void PracticeTools::SaveState() {
    if (!m_Context || !m_BML->IsPlaying())
        return;

    std::unique_ptr<Snapshot> snapshot(new Snapshot());
    const XObjectPointerArray &allObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::vector<CKObject *> objects(allObjects.Begin(), allObjects.End());
    snapshot->objects.reserve(objects.size());

    for (CKObject *object : objects) {
        if (!IsSnapshotObject(object))
            continue;
        CKStateChunk *chunk = CKSaveObjectState(object, CK_STATESAVE_ALL);
        if (chunk)
            snapshot->objects.push_back({object->GetID(), chunk});
    }

    if (TT_PhysicsCaptureSnapshot(m_Context, &snapshot->physics) != CK_OK) {
        for (ObjectState &state : snapshot->objects)
            DeleteCKStateChunk(state.chunk);
        m_BML->SendIngameMessage("Unable to capture physics state");
        return;
    }
    BML::Speedrun::TimerState timer{};
    snapshot->timerCaptured =
        BML::Speedrun::ReadTimerState(timer) == BML_OK &&
        BML::Speedrun::ReadTimerRunning(snapshot->timerRunning) == BML_OK;
    snapshot->timerElapsed = timer.ElapsedTime;

    ClearState();
    m_Snapshot = std::move(snapshot);
    GetLogger()->Info("Captured practice state: %zu Virtools objects%s.",
                      m_Snapshot->objects.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    m_BML->SendIngameMessage("Practice state saved");
}

void PracticeTools::RestoreState() {
    if (!m_Context || !m_BML->IsPlaying())
        return;
    if (!m_Snapshot) {
        m_BML->SendIngameMessage("No practice state has been saved");
        return;
    }

    // Parameters and behavior activation state are restored before entities;
    // IVP is restored last because it is authoritative for physical transforms.
    for (const ObjectState &state : m_Snapshot->objects) {
        CKObject *object = m_Context->GetObject(state.id);
        if (object && state.chunk)
            CKReadObjectState(object, state.chunk);
    }

    if (TT_PhysicsRestoreSnapshot(m_Context, m_Snapshot->physics) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore physics state");
        return;
    }
    if (m_Snapshot->timerCaptured &&
        BML::Speedrun::WriteTimerState(m_Snapshot->timerElapsed,
                                       m_Snapshot->timerRunning) != BML_OK) {
        GetLogger()->Warn("Could not restore the captured SR timer state.");
    }
    GetLogger()->Info("Restored practice state: %zu Virtools objects%s.",
                      m_Snapshot->objects.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    m_BML->SendIngameMessage("Practice state restored");
}

void PracticeTools::ClearState() {
    if (!m_Snapshot)
        return;
    for (ObjectState &state : m_Snapshot->objects)
        DeleteCKStateChunk(state.chunk);
    if (m_Snapshot->physics)
        TT_PhysicsDestroySnapshot(m_Snapshot->physics);
    m_Snapshot.reset();
}

void PracticeTools::OnPreLoadLevel() {
    EndFullSpeed();
    ClearState();
    m_SaveDown = false;
    m_RestoreDown = false;
}

void PracticeTools::OnPreExitLevel() {
    EndFullSpeed();
    ClearState();
    m_SaveDown = false;
    m_RestoreDown = false;
}

void PracticeTools::OnPreNextLevel() {
    EndFullSpeed();
    ClearState();
    m_SaveDown = false;
    m_RestoreDown = false;
}
