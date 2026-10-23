#include "PracticeTools.h"

#include <chrono>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#if defined(__linux__)
#include <dlfcn.h>
#endif

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

bool IsOpaqueRuntimeParameter(CKParameter *parameter) {
    if (!parameter || !CKIsChildClassOf(parameter, CKCID_PARAMETERLOCAL))
        return false;
    CKBehavior *owner = CKBehavior::Cast(parameter->GetOwner());
    if (!owner)
        return false;
    const CKGUID guid = owner->GetPrototypeGuid();
    // These building blocks keep C++ pointers/handles in local parameters.
    // CopyValue would only clone the address; after the pointed-to physics
    // object is rebuilt, restoring it creates a dangling pointer and a later
    // behavior reset double-frees the old allocation.
    return guid == CKGUID(0x36691920, 0x3b261630) || // TT Sky
           guid == CKGUID(0x5e624f0a, 0x35160450) || // Ball Joint
           guid == CKGUID(0x7435488d, 0x201d1188) || // Collision Detection
           guid == CKGUID(0x56e20c57, 0x0b926068) || // Physics Force
           guid == CKGUID(0x41cd3653, 0x0de60c1d) || // Hinge
           guid == CKGUID(0x2973360e, 0x23d31aa7) || // Slider
           guid == CKGUID(0x24a06a3a, 0x07100fce) || // Spring
           guid == CKGUID(0x199e4cf1, 0x545a78fe);   // Continuous Contact
}

#if defined(__linux__)
using CaptureToolboxTime = int (*)(CKContext *, std::uint64_t *, double *, CKBOOL *);
using RestoreToolboxTime = int (*)(CKContext *, std::uint64_t, double, CKBOOL);

CaptureToolboxTime ResolveCaptureToolboxTime() {
    static auto function = reinterpret_cast<CaptureToolboxTime>(
        dlsym(RTLD_DEFAULT, "TT_TimeCaptureRuntimeState"));
    return function;
}

RestoreToolboxTime ResolveRestoreToolboxTime() {
    static auto function = reinterpret_cast<RestoreToolboxTime>(
        dlsym(RTLD_DEFAULT, "TT_TimeRestoreRuntimeState"));
    return function;
}
#endif
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
    // CKContext::Reset can unload mods after it has already destroyed scene
    // objects. Snapshot value clones are registered CK objects and are owned
    // by that context too; dereferencing their cached pointers here can hit an
    // object whose ID/storage has already been recycled. Normal level changes
    // still call ClearState while the context is live. At final shutdown just
    // discard our non-owning bookkeeping and let CKContext finish teardown.
    m_Snapshot.reset();
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

    const auto captureStarted = std::chrono::steady_clock::now();
    // Keep the previous value objects alive and reuse them. Destroying and
    // recreating thousands of registered CKParameterOut objects made every
    // overwrite save much slower than the first one and deferred deletion
    // left a large amount of avoidable work for the following frame.
    std::unique_ptr<Snapshot> previous = std::move(m_Snapshot);
    std::unordered_set<CKObject *> privateParameters;
    std::unordered_map<CK_ID, ObjectState *> reusableParameters;
    std::unordered_map<CK_ID, ArrayState *> reusableArrays;
    if (previous) {
        for (ObjectState &state : previous->parameters) {
            if (state.value)
                privateParameters.insert(state.value);
            reusableParameters[state.id] = &state;
        }
        for (ArrayState &array : previous->arrays) {
            reusableArrays[array.id] = &array;
            for (ArrayCellState &cell : array.cells) {
                if (cell.parameter)
                    privateParameters.insert(cell.parameter);
            }
        }
    }
    std::unique_ptr<Snapshot> snapshot(new Snapshot());
    const XObjectPointerArray &allObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::vector<CKObject *> objects(allObjects.Begin(), allObjects.End());
    std::unordered_set<CKObject *> liveObjects(objects.begin(), objects.end());
    size_t invalidArrayParameters = 0;
    CKScene *scene = m_Context->GetCurrentScene();

    for (CKObject *object : objects) {
        if (!object || object->IsToBeDeleted() ||
            privateParameters.find(object) != privateParameters.end())
            continue;

        snapshot->identities[object->GetID()] = object;

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
            snapshot->behaviorIos.push_back(
                {object->GetID(), io->GetOldFlags(), io->IsActive() != FALSE});
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
            CKSceneObjectDesc *desc = scene->GetSceneObjectDesc(sceneObject);
            snapshot->sceneObjects.push_back(
                {object->GetID(), desc ? desc->m_Flags : 0, desc != nullptr});
        }

        if (CKIsChildClassOf(cid, CKCID_GROUP)) {
            auto *group = static_cast<CKGroup *>(object);
            GroupState state;
            state.id = object->GetID();
            for (int i = 0; i < group->GetObjectCount(); ++i) {
                if (CKBeObject *member = group->GetObject(i))
                    state.members.push_back(member->GetID());
            }
            snapshot->groups.push_back(std::move(state));
        }

        if (CKIsChildClassOf(cid, CKCID_PARAMETEROUT) ||
            CKIsChildClassOf(cid, CKCID_PARAMETERLOCAL)) {
            auto *parameter = static_cast<CKParameter *>(object);
            if (IsOpaqueRuntimeParameter(parameter))
                continue;
            // Array-owned cells are captured below using their stable
            // row/column identity. Capturing them here too doubles both the
            // memory and CopyValue work and was the reason the original code
            // needed a special second pass in the first place.
            if (CKDataArray::Cast(parameter->GetOwner()))
                continue;
            CKParameterOut *copy = nullptr;
            auto reusable = reusableParameters.find(object->GetID());
            if (reusable != reusableParameters.end() && reusable->second->value &&
                reusable->second->value->GetGUID() == parameter->GetGUID()) {
                copy = reusable->second->value;
                reusable->second->value = nullptr;
            } else {
                copy = m_Context->CreateCKParameterOut(
                    nullptr, parameter->GetGUID(), TRUE);
            }
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
            ArrayState *oldArray = nullptr;
            auto reusableArray = reusableArrays.find(state.id);
            if (reusableArray != reusableArrays.end() &&
                reusableArray->second->rows == state.rows &&
                reusableArray->second->columns == state.columns) {
                oldArray = reusableArray->second;
            }
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
                        // A data array can temporarily retain a stale pointer
                        // while a deferred deletion is pending. Never
                        // dereference a cell until the object manager's live
                        // set confirms that the pointer is still registered.
                        if (parameter && liveObjects.find(parameter) != liveObjects.end() &&
                            !parameter->IsToBeDeleted() &&
                            CKIsChildClassOf(parameter, CKCID_PARAMETER)) {
                            if (oldArray) {
                                const size_t index = static_cast<size_t>(row) * state.columns + column;
                                if (index < oldArray->cells.size()) {
                                    ArrayCellState &oldCell = oldArray->cells[index];
                                    if (oldCell.parameter &&
                                        oldCell.parameter->GetGUID() == parameter->GetGUID()) {
                                        cell.parameter = oldCell.parameter;
                                        oldCell.parameter = nullptr;
                                    }
                                }
                            }
                            if (!cell.parameter)
                                cell.parameter = m_Context->CreateCKParameterOut(
                                    nullptr, parameter->GetGUID(), TRUE);
                            if (cell.parameter &&
                                cell.parameter->CopyValue(parameter, FALSE) != CK_OK) {
                                m_Context->DestroyObject(cell.parameter);
                                cell.parameter = nullptr;
                                ++invalidArrayParameters;
                            }
                        } else if (parameter) {
                            ++invalidArrayParameters;
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
            CK3dEntity *parent = entity->GetParent();
            snapshot->entities.push_back(
                {object->GetID(), parent ? parent->GetID() : 0,
                 entity->GetLocalMatrix(), entity->IsVisible() != FALSE});
        } else if (CKIsChildClassOf(cid, CKCID_2DENTITY)) {
            auto *entity = static_cast<CK2dEntity *>(object);
            Entity2dState state;
            state.id = object->GetID();
            CK2dEntity *parent = entity->GetParent();
            state.parent = parent ? parent->GetID() : 0;
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

    if (CKBehaviorManager *behaviors = m_Context->GetBehaviorManager()) {
        for (int i = 0; i < behaviors->GetObjectsCount(); ++i) {
            if (CKBeObject *object = behaviors->GetObject(i))
                snapshot->activeBehaviorObjects.push_back(object->GetID());
        }
        const int pendingCount = behaviors->GetPendingObjectCount();
        for (int i = 0; i < pendingCount; ++i) {
            CK_ID id = 0;
            int operation = 0;
            if (behaviors->GetPendingObject(i, id, operation)) {
                snapshot->pendingBehaviorObjects.push_back(id);
                snapshot->pendingBehaviorOperations.push_back(operation);
            }
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
        DestroySnapshot(snapshot.get());
        DestroySnapshot(previous.get());
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
#if defined(__APPLE__)
    snapshot->toolboxTimeCaptured = TT_TimeCaptureRuntimeState(
        m_Context, &snapshot->toolboxFixedTicks, &snapshot->toolboxTime,
        &snapshot->toolboxTiming) == CK_OK;
#elif defined(__linux__)
    if (CaptureToolboxTime capture = ResolveCaptureToolboxTime()) {
        snapshot->toolboxTimeCaptured = capture(
            m_Context, &snapshot->toolboxFixedTicks, &snapshot->toolboxTime,
            &snapshot->toolboxTiming) == CK_OK;
    }
#endif

    DestroySnapshot(previous.get());
    m_Snapshot = std::move(snapshot);
    // Capturing is synchronous and can take substantially longer than one
    // fixed tick.  Do not let Player repay that wall-clock pause by running a
    // burst of score/physics ticks after this callback returns.
    if (m_Time)
        m_Time->ResetRuntimeSchedule();
    const auto captureMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - captureStarted).count();
    GetLogger()->Info("Captured practice state: %zu parameters, %zu arrays, %zu 3D/%zu 2D entities, "
                      "%zu behaviors, %zu links, %zu object/%zu master animations, %zu message waits%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(), m_Snapshot->entities2d.size(),
                      m_Snapshot->behaviors.size(),
                      m_Snapshot->behaviorLinks.size(),
                      m_Snapshot->objectAnimations.size(), m_Snapshot->animations.size(),
                      m_Snapshot->messageWaits.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    GetLogger()->Info("Practice state capture completed in %lld ms.",
                      static_cast<long long>(captureMilliseconds));
    if (invalidArrayParameters)
        GetLogger()->Warn("Skipped %zu invalid data-array parameter cells.",
                          invalidArrayParameters);
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
    const XObjectPointerArray &restoreObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::unordered_set<CKObject *> restoreLiveObjects(
        restoreObjects.Begin(), restoreObjects.End());
    std::unordered_set<CK_ID> staleObjectIds;
    const auto resolve = [this, &staleObjectIds](CK_ID id) -> CKObject * {
        CKObject *object = m_Context->GetObject(id);
        auto identity = m_Snapshot->identities.find(id);
        if (identity != m_Snapshot->identities.end() && identity->second == object)
            return object;
        if (id)
            staleObjectIds.insert(id);
        return nullptr;
    };

    // Restore values without loading whole CKStateChunks. Whole-object loads
    // rewrite behavior/parameter wiring, and CKDataArray::Load destroys owned
    // parameters; both operations are inappropriate for a repeatable runtime
    // rewind and were responsible for lost controls and the second-restore
    // crash.
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
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
                if (parameter &&
                    restoreLiveObjects.find(parameter) != restoreLiveObjects.end() &&
                    !parameter->IsToBeDeleted() &&
                    CKIsChildClassOf(parameter, CKCID_PARAMETER) && cell.parameter &&
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
        auto *parameter = CKParameter::Cast(resolve(state.id));
        if (parameter && state.value && parameter->GetGUID() == state.value->GetGUID())
            parameter->CopyValue(state.value, FALSE);
    }

    int scoreAfterValues = 0;
    ReadArrayInt(m_BML, "Energy", 0, scoreAfterValues);

    // Scripts reparent the camera rig, checkpoint markers and collectible
    // effects. A saved local matrix only has meaning relative to its saved
    // parent. Detach first to avoid transient cycles, rebuild the hierarchy,
    // then restore matrices.
    size_t changed3dParents = 0;
    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(resolve(state.id));
        auto *parent = CK3dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (entity && entity->GetParent() != parent) {
            entity->SetParent(nullptr, TRUE);
            ++changed3dParents;
        }
    }
    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(resolve(state.id));
        auto *parent = CK3dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (entity && entity->GetParent() != parent)
            entity->SetParent(parent, TRUE);
    }
    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(resolve(state.id));
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
    size_t changed2dParents = 0;
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        auto *parent = CK2dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (entity && entity->GetParent() != parent) {
            entity->SetParent(nullptr);
            ++changed2dParents;
        }
    }
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        auto *parent = CK2dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (entity && entity->GetParent() != parent)
            entity->SetParent(parent);
    }
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
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
        auto *animation = CKObjectAnimation::Cast(resolve(state.id));
        if (animation)
            animation->SetStep(state.step, nullptr);
    }
    for (const AnimationState &state : m_Snapshot->animations) {
        auto *animation = CKAnimation::Cast(resolve(state.id));
        if (animation)
            animation->SetStep(state.step);
    }

    for (const StateObjectState &state : m_Snapshot->stateObjects) {
        auto *object = CKStateObject::Cast(resolve(state.id));
        if (object) {
            if (state.active)
                object->EnterState();
            else
                object->LeaveState();
        }
    }
    for (const SynchroState &state : m_Snapshot->synchros) {
        auto *object = CKSynchroObject::Cast(resolve(state.id));
        if (object)
            object->RestoreRendezVousState(
                state.waiters, state.arrived.data(), static_cast<int>(state.arrived.size()),
                state.passed.data(), static_cast<int>(state.passed.size()));
    }
    for (const CriticalSectionState &state : m_Snapshot->criticalSections) {
        auto *object = CKCriticalSectionObject::Cast(resolve(state.id));
        if (object)
            object->RestoreObjectInCriticalSection(state.owner);
    }

    // Collectibles and checkpoints move objects between Virtools groups to
    // represent available/consumed state. Visibility alone is insufficient:
    // restoring it without group membership leaves the renderer and gameplay
    // scripts disagreeing about which score balls still exist.
    for (const GroupState &state : m_Snapshot->groups) {
        auto *group = CKGroup::Cast(resolve(state.id));
        if (!group)
            continue;
        group->Clear();
        for (CK_ID memberId : state.members) {
            auto *member = CKBeObject::Cast(resolve(memberId));
            if (member)
                group->AddObject(member);
        }
    }

    CKScene *scene = m_Context->GetCurrentScene();
    size_t addedSceneObjects = 0;
    size_t removedSceneObjects = 0;
    if (scene) {
        // First restore present behavioral objects together with their owned
        // scripts. This is what CKLevel/ObjectLoader normally do; a raw
        // CKScene::AddObject only resurrects a collectible's renderable shell.
        scene->BeginAddSequence(TRUE);
        for (const SceneObjectState &state : m_Snapshot->sceneObjects) {
            auto *object = CKSceneObject::Cast(resolve(state.id));
            if (object && state.inScene && !scene->GetSceneObjectDesc(object)) {
                scene->AddObjectToScene(object, TRUE);
                ++addedSceneObjects;
            }
        }
        scene->BeginAddSequence(FALSE);

        // Dependency expansion can include a script whose saved state says it
        // was absent. Reconcile every captured scene object explicitly after
        // expansion so the final membership is exact rather than merely a
        // dependency closure.
        scene->BeginRemoveSequence(TRUE);
        for (const SceneObjectState &state : m_Snapshot->sceneObjects) {
            auto *object = CKSceneObject::Cast(resolve(state.id));
            if (object && !state.inScene && scene->GetSceneObjectDesc(object)) {
                scene->RemoveObject(object);
                ++removedSceneObjects;
            }
        }
        scene->BeginRemoveSequence(FALSE);

        scene->BeginAddSequence(TRUE);
        for (const SceneObjectState &state : m_Snapshot->sceneObjects) {
            auto *object = CKSceneObject::Cast(resolve(state.id));
            if (!object || !state.inScene)
                continue;
            if (!scene->GetSceneObjectDesc(object)) {
                scene->AddObject(object);
                ++addedSceneObjects;
            }
            scene->SetObjectFlags(object,
                static_cast<CK_SCENEOBJECT_FLAGS>(state.flags));
        }
        scene->BeginAddSequence(FALSE);
    }

    // Reconcile which IVP objects exist before restoring behavior IO. The
    // physics layer may need to execute Physicalize/Continuous Contact while
    // rebuilding objects; the scheduler pass below then puts every input and
    // output back at the exact saved activation phase.
    if (TT_PhysicsRestoreSnapshot(m_Context, m_Snapshot->physics) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore physics state");
        return;
    }

    // Restore the exact behavior scheduler phase without Load(), which would
    // detach and rebuild links and parameters.
    for (const BehaviorState &state : m_Snapshot->behaviors) {
        auto *behavior = CKBehavior::Cast(resolve(state.id));
        if (behavior)
            behavior->ClearDelayedLinks();
    }
    if (CKMessageManager *messages = m_Context->GetMessageManager()) {
        messages->ClearRuntimeWaits();
        for (const MessageWaitState &state : m_Snapshot->messageWaits) {
            auto *behavior = CKBehavior::Cast(resolve(state.behavior));
            auto *output = CKBehaviorIO::Cast(resolve(state.output));
            auto *object = CKBeObject::Cast(resolve(state.object));
            if (behavior)
                messages->AddRuntimeWait(state.type, behavior, output, object);
        }
    }

    size_t sceneMembershipMismatches = 0;
    if (scene) {
        for (const SceneObjectState &state : m_Snapshot->sceneObjects) {
            auto *object = CKSceneObject::Cast(resolve(state.id));
            if (object &&
                (scene->GetSceneObjectDesc(object) != nullptr) != state.inScene)
                ++sceneMembershipMismatches;
        }
    }
    for (const BehaviorLinkState &state : m_Snapshot->behaviorLinks) {
        auto *link = CKBehaviorLink::Cast(resolve(state.id));
        if (!link)
            continue;
        link->SetActivationDelay(state.activationDelay);
        link->SetRuntimeFlags(state.runtimeFlags & ~CKBL_OLD_IN_DELAYED_LIST);
    }
    for (const BehaviorIoState &state : m_Snapshot->behaviorIos) {
        auto *io = CKBehaviorIO::Cast(resolve(state.id));
        if (io) {
            io->SetOldFlags(state.flags);
            io->Activate(state.active ? TRUE : FALSE);
        }
    }
    for (const BehaviorState &state : m_Snapshot->behaviors) {
        auto *behavior = CKBehavior::Cast(resolve(state.id));
        if (!behavior)
            continue;
        const CKDWORD current = behavior->GetFlags();
        behavior->ModifyFlags(state.runtimeFlags,
                              (current & kBehaviorRuntimeFlags) & ~state.runtimeFlags);
        for (CK_ID linkId : state.delayedLinks) {
            auto *link = CKBehaviorLink::Cast(resolve(linkId));
            if (link)
                behavior->AddDelayedLink(link);
        }
    }

    // Scene flags and behavior flags are not sufficient to rewind execution:
    // collectible scripts remove their owner from CKBehaviorManager when
    // consumed. Restore the scheduler's active set and deferred mutations as
    // an atomic state instead of firing scene detach/attach callbacks. The
    // latter destroys private runtime objects (for example TT Sky's entity)
    // and made repeated restores both visually incorrect and crash-prone.
    if (CKBehaviorManager *behaviors = m_Context->GetBehaviorManager()) {
        const CKERROR result = behaviors->RestoreRuntimeObjects(
            m_Snapshot->activeBehaviorObjects.data(),
            static_cast<int>(m_Snapshot->activeBehaviorObjects.size()),
            m_Snapshot->pendingBehaviorObjects.data(),
            m_Snapshot->pendingBehaviorOperations.data(),
            static_cast<int>(m_Snapshot->pendingBehaviorObjects.size()));
        if (result != CK_OK)
            GetLogger()->Warn("Could not restore behavior scheduler state (%d).", result);
    }

    if (m_Snapshot->timerCaptured &&
        BML::Speedrun::WriteTimerState(m_Snapshot->timerElapsed,
                                       m_Snapshot->timerRunning) != BML_OK) {
        GetLogger()->Warn("Could not restore the captured SR timer state.");
    }
    if (m_Snapshot->clockCaptured)
        m_Time->RestoreRuntimeClock(m_Snapshot->absoluteTime, m_Snapshot->mainTick);
#if defined(__APPLE__)
    if (m_Snapshot->toolboxTimeCaptured)
        TT_TimeRestoreRuntimeState(m_Context, m_Snapshot->toolboxFixedTicks,
                                   m_Snapshot->toolboxTime,
                                   m_Snapshot->toolboxTiming);
#elif defined(__linux__)
    if (m_Snapshot->toolboxTimeCaptured) {
        if (RestoreToolboxTime restore = ResolveRestoreToolboxTime())
            restore(m_Context, m_Snapshot->toolboxFixedTicks,
                    m_Snapshot->toolboxTime, m_Snapshot->toolboxTiming);
    }
#endif
    // Reconcile the controller with the keyboard as it physically exists now.
    // This clears missed key-up latches without discarding a direction the
    // player is intentionally holding across the rewind.
    SynchronizeMovementInput();
    GetLogger()->Info("Restored practice state: %zu parameters, %zu arrays, %zu 3D/%zu 2D entities, "
                      "%zu behaviors, %zu links, %zu object/%zu master animations, %zu message waits%s.",
                      m_Snapshot->parameters.size(), m_Snapshot->arrays.size(),
                      m_Snapshot->entities.size(), m_Snapshot->entities2d.size(),
                      m_Snapshot->behaviors.size(),
                      m_Snapshot->behaviorLinks.size(),
                      m_Snapshot->objectAnimations.size(), m_Snapshot->animations.size(),
                      m_Snapshot->messageWaits.size(),
                      m_Snapshot->timerCaptured ? " and SR timer" : "");
    GetLogger()->Info(
        "Restore topology audit: %zu/%zu parent changes, %zu scene additions, "
        "%zu scene removals, %zu final scene mismatches, %zu stale object identities; "
        "%zu active/%zu pending behavior objects.",
        changed3dParents, changed2dParents, addedSceneObjects,
        removedSceneObjects, sceneMembershipMismatches, staleObjectIds.size(),
        m_Snapshot->activeBehaviorObjects.size(),
        m_Snapshot->pendingBehaviorObjects.size());
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
    DestroySnapshot(m_Snapshot.get());
    m_Snapshot.reset();
}

void PracticeTools::DestroySnapshot(Snapshot *snapshot) {
    if (!snapshot)
        return;
    if (snapshot->physics) {
        TT_PhysicsDestroySnapshot(snapshot->physics);
        snapshot->physics = nullptr;
    }
    if (m_Context) {
        for (ObjectState &state : snapshot->parameters) {
            if (state.value && !state.value->IsToBeDeleted())
                m_Context->DestroyObject(state.value);
            state.value = nullptr;
        }
        for (ArrayState &array : snapshot->arrays) {
            for (ArrayCellState &cell : array.cells) {
                if (cell.parameter && !cell.parameter->IsToBeDeleted())
                    m_Context->DestroyObject(cell.parameter);
                cell.parameter = nullptr;
            }
        }
    }
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

void PracticeTools::OnBallNavActive() {
    // Checkpoint respawns, ball transformers and launch sequences can all
    // reactivate the controller after SDL events occurred while it was
    // inactive. Reconcile movement keys with their real physical state: held
    // directions remain held, released directions deliver a release edge.
    SynchronizeMovementInput();
}

void PracticeTools::SynchronizeMovementInput() {
    if (!m_Input)
        return;
    unsigned char *keyboard = m_Input->GetKeyboardState();
    if (!keyboard)
        return;
    const CKDWORD keys[] = {CKKEY_UP, CKKEY_DOWN, CKKEY_LEFT, CKKEY_RIGHT};
    if (m_Input->SynchronizeKeyboardKeys(keys, 4)) {
        GetLogger()->Info(
            "Movement input synchronized to physical keyboard: U=%d D=%d L=%d R=%d.",
            keyboard[CKKEY_UP] == KS_PRESSED,
            keyboard[CKKEY_DOWN] == KS_PRESSED,
            keyboard[CKKEY_LEFT] == KS_PRESSED,
            keyboard[CKKEY_RIGHT] == KS_PRESSED);
        return;
    }
    // Other input managers already poll their native device. Without an API
    // that exposes physical state, preserving their result is safer than
    // fabricating four releases and losing legitimately held directions.
}
