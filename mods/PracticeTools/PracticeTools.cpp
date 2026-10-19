#include "PracticeTools.h"

#include <unordered_set>

namespace {
constexpr CKDWORD kBehaviorRuntimeFlags =
    CKBEHAVIOR_ACTIVE | CKBEHAVIOR_WAITSFORMESSAGE |
    CKBEHAVIOR_DEACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME |
    CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_LAUNCHEDONCE;

bool ReadArrayInt(IBML *bml, const char *name, int column, int &value) {
    CKDataArray *array = bml ? bml->GetArrayByName(name) : nullptr;
    return array && array->GetRowCount() > 0 && array->GetColumnCount() > column &&
        array->GetElementValue(0, column, &value) != FALSE;
}
}

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
    CKScene *scene = m_Context->GetCurrentScene();

    for (CKObject *object : objects) {
        if (!object || object->IsToBeDeleted())
            continue;

        snapshot->objectIds.push_back(object->GetID());

        const CK_CLASSID cid = object->GetClassID();
        if (CKIsChildClassOf(cid, CKCID_BEHAVIOR)) {
            auto *behavior = static_cast<CKBehavior *>(object);
            BehaviorState state;
            state.id = object->GetID();
            state.runtimeFlags = behavior->GetFlags() & kBehaviorRuntimeFlags;
            for (int i = 0; i < behavior->GetDelayedLinkCount(); ++i) {
                CKBehaviorLink *link = behavior->GetDelayedLink(i);
                if (link)
                    state.delayedLinks.push_back(link->GetID());
            }
            snapshot->behaviors.push_back(std::move(state));
        } else if (CKIsChildClassOf(cid, CKCID_BEHAVIORIO)) {
            auto *io = static_cast<CKBehaviorIO *>(object);
            snapshot->behaviorIos.push_back({object->GetID(), io->GetOldFlags()});
        } else if (CKIsChildClassOf(cid, CKCID_BEHAVIORLINK)) {
            auto *link = static_cast<CKBehaviorLink *>(object);
            snapshot->behaviorLinks.push_back(
                {object->GetID(), link->GetActivationDelay(), link->GetRuntimeFlags()});
        }

        if (CKIsChildClassOf(cid, CKCID_OBJECTANIMATION)) {
            auto *animation = static_cast<CKObjectAnimation *>(object);
            snapshot->objectAnimations.push_back(
                {object->GetID(), animation->GetCurrentStep()});
        }
        if (CKIsChildClassOf(cid, CKCID_ANIMATION)) {
            auto *animation = static_cast<CKAnimation *>(object);
            snapshot->animations.push_back({object->GetID(), animation->GetStep()});
        }
        if (CKIsChildClassOf(cid, CKCID_STATE)) {
            auto *stateObject = static_cast<CKStateObject *>(object);
            snapshot->stateObjects.push_back(
                {object->GetID(), stateObject->IsStateActive() != FALSE});
        }
        if (CKIsChildClassOf(cid, CKCID_SYNCHRO)) {
            auto *synchro = static_cast<CKSynchroObject *>(object);
            SynchroState state;
            state.id = object->GetID();
            state.waiters = synchro->GetRendezVousNumberOfWaiters();
            for (int i = 0; i < synchro->GetRendezVousNumberOfArrivedObjects(); ++i) {
                if (CKBeObject *arrived = synchro->GetRendezVousArrivedObject(i))
                    state.arrived.push_back(arrived->GetID());
            }
            for (int i = 0; i < synchro->GetRendezVousNumberOfPassedObjects(); ++i) {
                if (CKBeObject *passed = synchro->GetRendezVousPassedObject(i))
                    state.passed.push_back(passed->GetID());
            }
            snapshot->synchros.push_back(std::move(state));
        }
        if (CKIsChildClassOf(cid, CKCID_CRITICALSECTION)) {
            auto *section = static_cast<CKCriticalSectionObject *>(object);
            snapshot->criticalSections.push_back(
                {object->GetID(), section->GetObjectInCriticalSection()});
        }

        if (scene && CKIsChildClassOf(cid, CKCID_SCENEOBJECT)) {
            auto *sceneObject = static_cast<CKSceneObject *>(object);
            if (scene->GetSceneObjectDesc(sceneObject))
                snapshot->sceneObjects.push_back(
                    {object->GetID(), scene->IsObjectActive(sceneObject) != FALSE});
        }

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
                    // Clone parameter-cell values explicitly. Some dynamic
                    // array-owned parameters are not returned by the broad
                    // context enumeration, including authoritative gameplay
                    // values such as Energy.Points.
                    ArrayCellState cell;
                    cell.row = row;
                    cell.column = column;
                    cell.type = type;
                    if (type == CKARRAYTYPE_PARAMETER) {
                        CKUINTPTR *element = array->GetElement(row, column);
                        auto *parameter = element
                            ? reinterpret_cast<CKParameterOut *>(*element) : nullptr;
                        if (parameter) {
                            cell.parameter = m_Context->CreateCKParameterOut(
                                nullptr, parameter->GetGUID(), TRUE);
                            if (cell.parameter)
                                cell.parameter->CopyValue(parameter, FALSE);
                        }
                    } else if (type == CKARRAYTYPE_STRING) {
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
                {object->GetID(), entity->GetLocalMatrix(), entity->IsVisible() != FALSE});
        } else if (CKIsChildClassOf(cid, CKCID_2DENTITY)) {
            auto *entity = static_cast<CK2dEntity *>(object);
            Entity2dState state;
            state.id = object->GetID();
            state.flags = entity->GetFlags();
            state.zOrder = entity->GetZOrder();
            state.visible = entity->IsVisible() != FALSE;
            state.homogeneous = entity->GetHomogeneousRect(state.rect) == CK_OK;
            if (!state.homogeneous)
                entity->GetRect(state.rect);
            entity->GetSourceRect(state.sourceRect);
            if (CKIsChildClassOf(cid, CKCID_SPRITE))
                state.spriteSlot = static_cast<CKSprite *>(object)->GetCurrentSlot();
            snapshot->entities2d.push_back(state);
        }
    }

    if (CKMessageManager *messages = m_Context->GetMessageManager()) {
        for (int i = 0; i < messages->GetRuntimeWaitCount(); ++i) {
            CKMessageType type = -1;
            CKBehavior *behavior = nullptr;
            CKBehaviorIO *output = nullptr;
            CKBeObject *object = nullptr;
            if (messages->GetRuntimeWait(i, type, behavior, output, object)) {
                snapshot->messageWaits.push_back(
                    {type, behavior ? behavior->GetID() : 0,
                     output ? output->GetID() : 0, object ? object->GetID() : 0});
            }
        }
    }

    if (TT_PhysicsCaptureSnapshot(m_Context, &snapshot->physics) != CK_OK) {
        for (ObjectState &state : snapshot->parameters) {
            if (state.value)
                m_Context->DestroyObject(state.value);
        }
        for (ArrayState &array : snapshot->arrays) {
            for (ArrayCellState &cell : array.cells) {
                if (cell.parameter)
                    m_Context->DestroyObject(cell.parameter);
            }
        }
        m_BML->SendIngameMessage("Unable to capture physics state");
        return;
    }
    BML::Speedrun::TimerState timer{};
    snapshot->timerCaptured =
        BML::Speedrun::ReadTimerState(timer) == BML_OK &&
        BML::Speedrun::ReadTimerRunning(snapshot->timerRunning) == BML_OK;
    snapshot->timerElapsed = timer.ElapsedTime;
    snapshot->scoreCaptured =
        ReadArrayInt(m_BML, "Energy", 0, snapshot->energyPoints) &&
        ReadArrayInt(m_BML, "CurrentLevel", 5, snapshot->levelPoints);
    if (m_Time) {
        snapshot->absoluteTime = m_Time->GetAbsoluteTime();
        snapshot->mainTick = m_Time->GetMainTickCount();
        snapshot->clockCaptured = true;
    }

    m_Snapshot = std::move(snapshot);
    GetLogger()->Info("Captured practice state: %zu parameters, %zu arrays, %zu 3D/%zu 2D entities, "
                      "%zu behaviors, %zu links, %zu object/%zu master animations, %zu message waits%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(), m_Snapshot->entities2d.size(),
                      m_Snapshot->behaviors.size(),
                      m_Snapshot->behaviorLinks.size(),
                      m_Snapshot->objectAnimations.size(), m_Snapshot->animations.size(),
                      m_Snapshot->messageWaits.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    if (m_Snapshot->scoreCaptured)
        GetLogger()->Info("Snapshot score audit: Energy.Points=%d CurrentLevel.Points=%d.",
                          m_Snapshot->energyPoints, m_Snapshot->levelPoints);
    m_BML->SendIngameMessage("Practice state saved");
}

void PracticeTools::RestoreState() {
    if (!m_Context || !m_BML->IsPlaying())
        return;
    if (!m_Snapshot) {
        m_BML->SendIngameMessage("No practice state has been saved");
        return;
    }

    int scoreBefore = 0;
    ReadArrayInt(m_BML, "Energy", 0, scoreBefore);

    // Rewind object topology as well as object values. Award, death and
    // particle scripts can create dynamic scene objects after the snapshot;
    // leaving those alive makes each repeated restore accumulate another
    // visual instance. Snapshot-owned parameter clones are dynamic too, so
    // explicitly protect them from this pruning pass.
    std::unordered_set<CK_ID> savedObjects(
        m_Snapshot->objectIds.begin(), m_Snapshot->objectIds.end());
    std::unordered_set<CK_ID> snapshotObjects;
    for (const ObjectState &state : m_Snapshot->parameters) {
        if (state.value)
            snapshotObjects.insert(state.value->GetID());
    }
    for (const ArrayState &array : m_Snapshot->arrays) {
        for (const ArrayCellState &cell : array.cells) {
            if (cell.parameter)
                snapshotObjects.insert(cell.parameter->GetID());
        }
    }
    const XObjectPointerArray &currentObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::vector<CK_ID> createdAfterSnapshot;
    for (CKObject *object : currentObjects) {
        if (object && !object->IsToBeDeleted() && object->IsDynamic() &&
            savedObjects.find(object->GetID()) == savedObjects.end() &&
            snapshotObjects.find(object->GetID()) == snapshotObjects.end()) {
            createdAfterSnapshot.push_back(object->GetID());
        }
    }
    for (CK_ID id : createdAfterSnapshot) {
        if (CKObject *object = m_Context->GetObject(id))
            m_Context->DestroyObject(object, CK_DESTROY_TEMPOBJECT);
    }
    if (!createdAfterSnapshot.empty())
        GetLogger()->Info("Removed %zu post-snapshot dynamic objects.",
                          createdAfterSnapshot.size());

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
            if (cell.type == CKARRAYTYPE_PARAMETER) {
                CKUINTPTR *element = array->GetElement(cell.row, cell.column);
                auto *parameter = element
                    ? reinterpret_cast<CKParameterOut *>(*element) : nullptr;
                if (parameter && cell.parameter &&
                    parameter->GetGUID() == cell.parameter->GetGUID())
                    parameter->CopyValue(cell.parameter, FALSE);
            } else if (cell.type == CKARRAYTYPE_STRING)
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

    int scoreAfterValues = 0;
    ReadArrayInt(m_BML, "Energy", 0, scoreAfterValues);

    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(m_Context->GetObject(state.id));
        if (!entity)
            continue;
        // Restore the transform relative to its parent. Restoring world
        // matrices in object-list order makes every parent update its children
        // again, so camera rigs and orbiting bonus particles accumulate an
        // offset after each rewind.
        entity->SetLocalMatrix(state.local);
        entity->Show(state.visible ? CKSHOW : CKHIDE);
    }

    // 2D HUD elements have their own hierarchy and animation state. Keep
    // children fixed while restoring each saved rect so parent updates cannot
    // accumulate offsets over repeated restores.
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(m_Context->GetObject(state.id));
        if (!entity)
            continue;
        entity->SetFlags(state.flags);
        if (state.homogeneous)
            entity->SetHomogeneousRect(state.rect, TRUE);
        else
            entity->SetRect(state.rect, TRUE);
        VxRect sourceRect = state.sourceRect;
        entity->SetSourceRect(sourceRect);
        entity->SetZOrder(state.zOrder);
        entity->Show(state.visible ? CKSHOW : CKHIDE);
        if (state.spriteSlot >= 0 && CKIsChildClassOf(entity->GetClassID(), CKCID_SPRITE))
            static_cast<CKSprite *>(entity)->SetCurrentSlot(state.spriteSlot);
    }

    // Restore animation cursors before scheduler state. SetStep reapplies the
    // pose belonging to the saved phase; the restored behavior graph then
    // continues advancing it on the next simulation tick.
    for (const ObjectAnimationState &state : m_Snapshot->objectAnimations) {
        auto *animation = CKObjectAnimation::Cast(m_Context->GetObject(state.id));
        if (animation)
            animation->SetStep(state.step, nullptr);
    }
    for (const AnimationState &state : m_Snapshot->animations) {
        auto *animation = CKAnimation::Cast(m_Context->GetObject(state.id));
        if (animation)
            animation->SetStep(state.step);
    }

    for (const StateObjectState &state : m_Snapshot->stateObjects) {
        auto *object = CKStateObject::Cast(m_Context->GetObject(state.id));
        if (object) {
            if (state.active)
                object->EnterState();
            else
                object->LeaveState();
        }
    }
    for (const SynchroState &state : m_Snapshot->synchros) {
        auto *object = CKSynchroObject::Cast(m_Context->GetObject(state.id));
        if (object)
            object->RestoreRendezVousState(
                state.waiters, state.arrived.data(), static_cast<int>(state.arrived.size()),
                state.passed.data(), static_cast<int>(state.passed.size()));
    }
    for (const CriticalSectionState &state : m_Snapshot->criticalSections) {
        auto *object = CKCriticalSectionObject::Cast(m_Context->GetObject(state.id));
        if (object)
            object->RestoreObjectInCriticalSection(state.owner);
    }

    CKScene *scene = m_Context->GetCurrentScene();
    if (scene) {
        for (const SceneObjectState &state : m_Snapshot->sceneObjects) {
            auto *object = CKSceneObject::Cast(m_Context->GetObject(state.id));
            if (!object || !scene->GetSceneObjectDesc(object))
                continue;
            scene->ModifyObjectFlags(object,
                state.active ? CK_SCENEOBJECT_ACTIVE : 0,
                state.active ? 0 : CK_SCENEOBJECT_ACTIVE);
        }
    }

    // Restore the exact behavior scheduler phase without Load(), which would
    // detach and rebuild links and parameters.
    for (const BehaviorState &state : m_Snapshot->behaviors) {
        auto *behavior = CKBehavior::Cast(m_Context->GetObject(state.id));
        if (behavior)
            behavior->ClearDelayedLinks();
    }
    if (CKMessageManager *messages = m_Context->GetMessageManager()) {
        messages->ClearRuntimeWaits();
        for (const MessageWaitState &state : m_Snapshot->messageWaits) {
            auto *behavior = CKBehavior::Cast(m_Context->GetObject(state.behavior));
            auto *output = CKBehaviorIO::Cast(m_Context->GetObject(state.output));
            auto *object = CKBeObject::Cast(m_Context->GetObject(state.object));
            if (behavior)
                messages->AddRuntimeWait(state.type, behavior, output, object);
        }
    }
    for (const BehaviorLinkState &state : m_Snapshot->behaviorLinks) {
        auto *link = CKBehaviorLink::Cast(m_Context->GetObject(state.id));
        if (!link)
            continue;
        link->SetActivationDelay(state.activationDelay);
        link->SetRuntimeFlags(state.runtimeFlags & ~CKBL_OLD_IN_DELAYED_LIST);
    }
    for (const BehaviorIoState &state : m_Snapshot->behaviorIos) {
        auto *io = CKBehaviorIO::Cast(m_Context->GetObject(state.id));
        if (io)
            io->SetOldFlags(state.flags);
    }
    for (const BehaviorState &state : m_Snapshot->behaviors) {
        auto *behavior = CKBehavior::Cast(m_Context->GetObject(state.id));
        if (!behavior)
            continue;
        const CKDWORD current = behavior->GetFlags();
        behavior->ModifyFlags(state.runtimeFlags,
                              (current & kBehaviorRuntimeFlags) & ~state.runtimeFlags);
        for (CK_ID linkId : state.delayedLinks) {
            auto *link = CKBehaviorLink::Cast(m_Context->GetObject(linkId));
            if (link)
                behavior->AddDelayedLink(link);
        }
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
    if (m_Snapshot->clockCaptured)
        m_Time->RestoreRuntimeClock(m_Snapshot->absoluteTime, m_Snapshot->mainTick);
    GetLogger()->Info("Restored practice state: %zu parameters, %zu arrays, %zu 3D/%zu 2D entities, "
                      "%zu behaviors, %zu links, %zu object/%zu master animations, %zu message waits%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(), m_Snapshot->entities2d.size(),
                      m_Snapshot->behaviors.size(),
                      m_Snapshot->behaviorLinks.size(),
                      m_Snapshot->objectAnimations.size(), m_Snapshot->animations.size(),
                      m_Snapshot->messageWaits.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    if (m_Snapshot->scoreCaptured) {
        int scoreAfter = 0;
        int levelAfter = 0;
        ReadArrayInt(m_BML, "Energy", 0, scoreAfter);
        ReadArrayInt(m_BML, "CurrentLevel", 5, levelAfter);
        GetLogger()->Info(
            "Restore score audit: before=%d after-values=%d final=%d/%d expected=%d/%d.",
            scoreBefore, scoreAfterValues, scoreAfter, levelAfter,
            m_Snapshot->energyPoints, m_Snapshot->levelPoints);
    }
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
        for (ArrayState &array : m_Snapshot->arrays) {
            for (ArrayCellState &cell : array.cells) {
                if (cell.parameter && !cell.parameter->IsToBeDeleted())
                    m_Context->DestroyObject(cell.parameter);
            }
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
