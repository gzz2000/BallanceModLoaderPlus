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
    // The temporary override is intentionally global. In particular it must
    // also release an explicit practice cap during the intro and menus.
    const bool fullSpeedDown = m_FullSpeedKey && m_Input->IsKeyDown(m_FullSpeedKey);
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
    m_PreviousProcessRate = m_Time->GetFixedProcessRate();
    m_Time->SetFixedProcessRate(264.0f);
    m_Time->ChangeLimitOptions(CK_FRAMERATE_FREE);
    m_FullSpeedActive = true;
    GetLogger()->Info("Full-speed override enabled (264 samples/s).");
}

void PracticeTools::EndFullSpeed() {
    if (!m_FullSpeedActive || !m_Time)
        return;
    if (m_PreviousFrameMode == CK_FRAMERATE_LIMIT && m_PreviousFrameLimit > 0.0f)
        m_Time->SetFrameRateLimit(m_PreviousFrameLimit);
    if (m_PreviousProcessRate > 0.0f)
        m_Time->SetFixedProcessRate(m_PreviousProcessRate);
    m_Time->ChangeLimitOptions(m_PreviousFrameMode);
    m_FullSpeedActive = false;
    GetLogger()->Info("Full-speed override released; restored mode %d at %.3f Hz.",
                      static_cast<int>(m_PreviousFrameMode), m_PreviousFrameLimit);
}

void PracticeTools::SaveState() {
    if (!m_Context || !m_BML->IsPlaying())
        return;

    // Snapshot values are represented by temporary dynamic parameters. Drop
    // the previous snapshot before enumerating the context so those private
    // copies can never recursively become part of the next snapshot.
    ClearState();
    std::unique_ptr<Snapshot> snapshot(new Snapshot());
    const XObjectPointerArray &allObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::vector<CKObject *> objects(allObjects.Begin(), allObjects.End());

    for (CKObject *object : objects) {
        if (!object || object->IsToBeDeleted())
            continue;

        const CK_CLASSID cid = object->GetClassID();
        if (CKIsChildClassOf(cid, CKCID_PARAMETEROUT) ||
            CKIsChildClassOf(cid, CKCID_PARAMETERLOCAL)) {
            auto *parameter = static_cast<CKParameter *>(object);
            CKParameterOut *copy = m_Context->CreateCKParameterOut(
                nullptr, parameter->GetGUID(), TRUE);
            if (copy && copy->CopyValue(parameter, FALSE) == CK_OK)
                snapshot->parameters.push_back({object->GetID(), copy});
            else if (copy)
                m_Context->DestroyObject(copy);
            continue;
        }

        if (CKIsChildClassOf(cid, CKCID_DATAARRAY)) {
            auto *array = static_cast<CKDataArray *>(object);
            ArrayState state;
            state.id = object->GetID();
            state.rows = array->GetRowCount();
            state.columns = array->GetColumnCount();
            for (int row = 0; row < state.rows; ++row) {
                for (int column = 0; column < state.columns; ++column) {
                    const CK_ARRAYTYPE type = array->GetColumnType(column);
                    // Parameter cells are already captured through their
                    // CKParameterOut objects above. Never rebuild array
                    // structure: CKDataArray::Load destroys those objects and
                    // made a saved state unsafe to restore more than once.
                    if (type == CKARRAYTYPE_PARAMETER)
                        continue;
                    ArrayCellState cell;
                    cell.row = row;
                    cell.column = column;
                    cell.type = type;
                    if (type == CKARRAYTYPE_STRING) {
                        CKSTRING value = nullptr;
                        if (array->GetElementValue(row, column, &value) && value)
                            cell.string = value;
                    } else {
                        array->GetElementValue(row, column, &cell.scalar);
                    }
                    state.cells.push_back(std::move(cell));
                }
            }
            snapshot->arrays.push_back(std::move(state));
            continue;
        }

        if (CKIsChildClassOf(cid, CKCID_3DENTITY)) {
            auto *entity = static_cast<CK3dEntity *>(object);
            snapshot->entities.push_back(
                {object->GetID(), entity->GetWorldMatrix(), entity->IsVisible() != FALSE});
        }
    }

    if (TT_PhysicsCaptureSnapshot(m_Context, &snapshot->physics) != CK_OK) {
        for (ObjectState &state : snapshot->parameters) {
            if (state.value)
                m_Context->DestroyObject(state.value);
        }
        m_BML->SendIngameMessage("Unable to capture physics state");
        return;
    }
    BML::Speedrun::TimerState timer{};
    snapshot->timerCaptured =
        BML::Speedrun::ReadTimerState(timer) == BML_OK &&
        BML::Speedrun::ReadTimerRunning(snapshot->timerRunning) == BML_OK;
    snapshot->timerElapsed = timer.ElapsedTime;

    m_Snapshot = std::move(snapshot);
    GetLogger()->Info("Captured practice state: %zu parameters, %zu arrays, %zu entities%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(),
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

    // Restore values without loading whole CKStateChunks. Whole-object loads
    // rewrite behavior/parameter wiring, and CKDataArray::Load destroys owned
    // parameters; both operations are inappropriate for a repeatable runtime
    // rewind and were responsible for lost controls and the second-restore
    // crash.
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(m_Context->GetObject(state.id));
        if (!array || array->GetRowCount() != state.rows ||
            array->GetColumnCount() != state.columns) {
            GetLogger()->Warn("Skipped changed data array %u during restore.", state.id);
            continue;
        }
        for (const ArrayCellState &cell : state.cells) {
            if (array->GetColumnType(cell.column) != cell.type)
                continue;
            if (cell.type == CKARRAYTYPE_STRING)
                array->SetElementValue(cell.row, cell.column,
                                       const_cast<char *>(cell.string.c_str()));
            else
                array->SetElementValue(cell.row, cell.column,
                                       const_cast<CKDWORD *>(&cell.scalar));
        }
    }

    for (const ObjectState &state : m_Snapshot->parameters) {
        auto *parameter = CKParameter::Cast(m_Context->GetObject(state.id));
        if (parameter && state.value && parameter->GetGUID() == state.value->GetGUID())
            parameter->CopyValue(state.value, FALSE);
    }

    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(m_Context->GetObject(state.id));
        if (!entity)
            continue;
        entity->SetWorldMatrix(state.world);
        entity->Show(state.visible ? CKSHOW : CKHIDE);
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
    GetLogger()->Info("Restored practice state: %zu parameters, %zu arrays, %zu entities%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    m_BML->SendIngameMessage("Practice state restored");
}

void PracticeTools::ClearState() {
    if (!m_Snapshot)
        return;
    if (m_Snapshot->physics)
        TT_PhysicsDestroySnapshot(m_Snapshot->physics);
    if (m_Context) {
        for (ObjectState &state : m_Snapshot->parameters) {
            if (state.value && !state.value->IsToBeDeleted())
                m_Context->DestroyObject(state.value);
        }
    }
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
