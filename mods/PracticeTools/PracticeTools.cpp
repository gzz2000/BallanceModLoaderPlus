#include "PracticeTools.h"
#include "TimelineObjects.h"
#include "ArrayTopology.h"
#include "OwnedParameters.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {
#ifdef __APPLE__
void *ResolveEntityTransformSymbol(const char *symbol) {
    // CK2_3D is loaded RTLD_LOCAL, so the global namespace is insufficient.
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *image = _dyld_get_image_name(i);
        if (!image || !std::strstr(image, "/libCK2_3D.")) continue;
        if (void *handle = dlopen(image, RTLD_LAZY | RTLD_NOLOAD)) {
            void *result = dlsym(handle, symbol);
            dlclose(handle);
            if (result) return result;
        }
    }
    return nullptr;
}
void *OpenSoundSnapshotProvider() {
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *image = _dyld_get_image_name(i);
        const char *name = image ? std::strrchr(image, '/') : nullptr;
        if (name && std::strcmp(name + 1, "libSdlSoundManager.dylib") == 0)
            return dlopen(image, RTLD_LAZY | RTLD_NOLOAD);
    }
    return nullptr;
}
using CaptureEntity2d = int (*)(CKObject *, CKEntity2dState *, std::size_t);
using RestoreEntities2d = int (*)(CKContext *, const CKEntity2dRestoreEntry *, std::size_t, std::size_t);
using CaptureEntityTransform = int (*)(CKObject *, CKEntityTransformState *, std::size_t);
using RestoreEntityTransform = int (*)(CKObject *, const CKEntityTransformState *, std::size_t);
using CaptureEntityUserBox = int (*)(CKObject *, CKEntityUserBoxState *, std::size_t);
using RestoreEntityUserBox = int (*)(CKObject *, const CKEntityUserBoxState *, std::size_t);
using CaptureEntityVisibility = int (*)(CKObject *, CKEntityVisibilityState *, std::size_t);
using RestoreEntityVisibility = int (*)(CKObject *, const CKEntityVisibilityState *, std::size_t);
void *OpenLegacySegmentProvider() {
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *image = _dyld_get_image_name(i);
        const char *name = image ? std::strrchr(image, '/') : nullptr;
        if (name && std::strcmp(name + 1, "LegacyTASRuntime.bmodp") == 0)
            return dlopen(image, RTLD_LAZY | RTLD_NOLOAD);
    }
    return nullptr;
}
#endif
bool TasOwnsKeyboard(CKInputManager *input) {
#ifdef __APPLE__
    // Mods are loaded locally, so RTLD_DEFAULT need not expose their exports.
    // Resolve on demand: retaining a function pointer across mod unload is unsafe.
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *image = _dyld_get_image_name(i);
        if (!image || !std::strstr(image, "BallanceTAS.bmodp"))
            continue;
        if (void *handle = dlopen(image, RTLD_LAZY | RTLD_NOLOAD)) {
            auto query = reinterpret_cast<int (*)(CKInputManager *)>(
                dlsym(handle, "BML_TASOwnsKeyboard"));
            const bool owns = query && query(input);
            dlclose(handle);
            if (owns) return true;
        }
    }
#endif
    return false;
}

constexpr CKDWORD kBehaviorRuntimeFlags =
    CKBEHAVIOR_ACTIVE | CKBEHAVIOR_WAITSFORMESSAGE |
    CKBEHAVIOR_DEACTIVATENEXTFRAME | CKBEHAVIOR_RESETNEXTFRAME |
    CKBEHAVIOR_ACTIVATENEXTFRAME | CKBEHAVIOR_LAUNCHEDONCE;

void AuditTimelineReferences(CKContext *context, ILogger *logger,
    const std::vector<std::pair<CK_ID, CKObject *>> &identities, const char *phase) {
    if (identities.empty()) return;
    // Copy the list: CKContext reuses its enumeration buffer for other queries.
    const XObjectPointerArray objects = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::unordered_map<CKObject *, CK_ID> live;
    for (auto *object : objects) if (object) live.emplace(object, object->GetID());
    const auto idOf = [&live](CKObject *object) {
        const auto found = live.find(object);
        return found == live.end() ? CK_ID(0) : found->second;
    };
    for (const auto &identity : identities) {
        auto *object = context->GetObject(identity.first);
        if (!object || object != identity.second) {
            logger->Info("Timeline reference audit phase=%s id=%u missing=1.", phase, unsigned(identity.first));
            continue;
        }
        CK_ID parent = 0, material = 0;
        if (auto *entity = CK2dEntity::Cast(object)) {
            parent = idOf(entity->GetParent());
            material = idOf(entity->GetMaterial());
        }
        auto *sceneObject = CKSceneObject::Cast(object);
        auto *scene = context->GetCurrentScene();
        logger->Info("Timeline reference audit phase=%s id=%u class=%d name=%s flags=%08x dynamic=%d parent=%u material=%u in_scene=%d.",
            phase, unsigned(identity.first), int(object->GetClassID()), object->GetName() ? object->GetName() : "<null>",
            unsigned(object->GetObjectFlags()), int(object->IsDynamic()), unsigned(parent), unsigned(material),
            int(scene && sceneObject && scene->GetSceneObjectDesc(sceneObject)));
        unsigned parameters = 0, cells = 0, groups = 0, materials = 0;
        for (auto *source : objects) {
            if (!source || source->IsToBeDeleted()) continue;
            if (auto *parameter = CKParameter::Cast(source)) {
                if (parameter->GetParameterClassID() && parameter->GetValueObject(FALSE) == object) {
                    ++parameters;
                    logger->Info("Timeline reference edge phase=%s target=%u parameter=%u owner=%u name=%s.",
                        phase, unsigned(identity.first), unsigned(source->GetID()), unsigned(idOf(parameter->GetOwner())),
                        source->GetName() ? source->GetName() : "<null>");
                }
            }
            if (auto *array = CKDataArray::Cast(source)) {
                for (int column = 0; column < array->GetColumnCount(); ++column) {
                    if (array->GetColumnType(column) != CKARRAYTYPE_OBJECT) continue;
                    for (int row = 0; row < array->GetRowCount(); ++row) {
                        CK_ID value = 0;
                        if (array->GetElementValue(row, column, &value) && value == identity.first) {
                            ++cells;
                            logger->Info("Timeline reference edge phase=%s target=%u array=%u name=%s row=%d column=%d.",
                                phase, unsigned(identity.first), unsigned(source->GetID()),
                                source->GetName() ? source->GetName() : "<null>", row, column);
                        }
                    }
                }
            }
            if (auto *group = CKGroup::Cast(source))
                for (int i = 0; i < group->GetObjectCount(); ++i) if (group->GetObject(i) == object) ++groups;
            if (auto *entity = CK2dEntity::Cast(source)) if (entity->GetMaterial() == object) ++materials;
        }
        logger->Info("Timeline reference totals phase=%s id=%u parameters=%u object_cells=%u group_memberships=%u materials_2d=%u.",
            phase, unsigned(identity.first), parameters, cells, groups, materials);
    }
}

bool ReadArrayInt(IBML *bml, const char *name, int column, int &value) {
    CKDataArray *array = bml ? bml->GetArrayByName(name) : nullptr;
    return array && array->GetRowCount() > 0 && array->GetColumnCount() > column &&
        array->GetElementValue(0, column, &value) != FALSE;
}

bool IsOpaqueRuntimeParameter(CKParameter *parameter) {
    if (!parameter)
        return false;
    const CKGUID type = parameter->GetGUID();
    // POINTER is always an address owned by a building block. VOIDBUF has no
    // type or ownership metadata and several Ballance blocks place object
    // pointers and linked-list roots inside it. A byte-for-byte rewind can
    // therefore never be made safe generically. Semantic state living behind
    // either type is captured by its owning runtime module instead.
    return type == CKPGUID_POINTER || type == CKPGUID_VOIDBUF;
}

bool IsOwnedBySnapshotStorage(
    CKObject *object, const std::unordered_set<CKObject *> &storageRoots) {
    // Structured CK parameters create registered child CKParameterOut objects
    // (Floor, Obstacle, etc.).  The children are not listed in our vectors,
    // but they are owned recursively by the private top-level clone.  Treat
    // the whole ownership tree as snapshot storage; otherwise a later save
    // mistakes those children for newly created gameplay state and recursively
    // snapshots the snapshot itself.
    CKObject *current = object;
    for (int depth = 0; current && depth < 32; ++depth) {
        if (storageRoots.count(current))
            return true;
        CKParameter *parameter = CKParameter::Cast(current);
        if (!parameter)
            return false;
        CKObject *owner = parameter->GetOwner();
        if (!owner || owner == current)
            return false;
        current = owner;
    }
    return false;
}

void CollectSnapshotParameterStorage(
    CKContext *context, CKParameter *parameter,
    std::unordered_set<CKObject *> &storage) {
    if (!context || !parameter || !storage.insert(parameter).second)
        return;
    CKParameterManager *manager = context->GetParameterManager();
    CKStructStruct *structure = manager
        ? manager->GetStructDescByType(parameter->GetType()) : nullptr;
    if (!structure)
        return;
    auto *members = static_cast<CK_ID *>(parameter->GetReadDataPtr(FALSE));
    if (!members)
        return;
    for (int i = 0; i < structure->NbData; ++i) {
        CollectSnapshotParameterStorage(
            context, CKParameter::Cast(context->GetObject(members[i])), storage);
    }
}

void AuditOpaqueRuntimeParameters(CKContext *context, ILogger *logger) {
    if (!context || !logger || !std::getenv("BALLANCE_SNAPSHOT_AUDIT"))
        return;

    std::set<std::pair<CKDWORD, CKDWORD>> reported;
    const XObjectPointerArray &behaviors =
        context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
    for (XObjectPointerArray::ConstIterator it = behaviors.Begin();
         it != behaviors.End(); ++it) {
        auto *behavior = static_cast<CKBehavior *>(*it);
        bool hasOpaqueLocal = false;
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) {
            CKParameterLocal *parameter = behavior->GetLocalParameter(i);
            if (parameter && (parameter->GetGUID() == CKPGUID_POINTER ||
                              parameter->GetGUID() == CKPGUID_VOIDBUF)) {
                hasOpaqueLocal = true;
                break;
            }
        }
        if (!hasOpaqueLocal)
            continue;

        const CKGUID guid = behavior->GetPrototypeGuid();
        if (!reported.insert({guid.d1, guid.d2}).second)
            continue;
        logger->Info("Snapshot opaque audit: behavior=%s guid=%08x:%08x locals=%d.",
                     behavior->GetName() ? behavior->GetName() : "<unnamed>",
                     static_cast<unsigned>(guid.d1),
                     static_cast<unsigned>(guid.d2),
                     behavior->GetLocalParameterCount());
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) {
            CKParameterLocal *parameter = behavior->GetLocalParameter(i);
            if (!parameter || (parameter->GetGUID() != CKPGUID_POINTER &&
                               parameter->GetGUID() != CKPGUID_VOIDBUF))
                continue;
            const CKGUID type = parameter->GetGUID();
            logger->Info("  local[%d]=%s type=%s size=%d.", i,
                         parameter->GetName() ? parameter->GetName() : "<unnamed>",
                         type == CKPGUID_POINTER ? "pointer" : "voidbuf",
                         parameter->GetDataSize());
        }
    }
}

#if defined(__linux__)
using CaptureToolboxTime = int (*)(CKContext *, std::uint64_t *, double *, CKBOOL *, CKBOOL *);
using RestoreToolboxTime = int (*)(CKContext *, std::uint64_t, double, CKBOOL, CKBOOL);

CaptureToolboxTime ResolveCaptureToolboxTime() {
    static auto function = reinterpret_cast<CaptureToolboxTime>(
        dlsym(RTLD_DEFAULT, "TT_TimeCaptureRuntimeStateV2"));
    return function;
}

RestoreToolboxTime ResolveRestoreToolboxTime() {
    static auto function = reinterpret_cast<RestoreToolboxTime>(
        dlsym(RTLD_DEFAULT, "TT_TimeRestoreRuntimeStateV2"));
    return function;
}
#endif
}

static PracticeTools *g_PracticeTools = nullptr;

IMod *BMLEntry(IBML *bml) {
    g_PracticeTools = new PracticeTools(bml);
    return g_PracticeTools;
}

void BMLExit(IMod *mod) {
    if (g_PracticeTools == mod)
        g_PracticeTools = nullptr;
    delete mod;
}

extern "C" __attribute__((visibility("default"))) int BML_PracticeCaptureState() {
    return g_PracticeTools && g_PracticeTools->CaptureForAutomation() ? 1 : 0;
}

extern "C" __attribute__((visibility("default"))) int BML_PracticeRestoreState() {
    return g_PracticeTools && g_PracticeTools->RestoreForAutomation() ? 1 : 0;
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
    m_ReusableSnapshot.reset();
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

    const bool inLevel = m_BML->IsPlaying() || m_BML->IsPaused();
    // The temporary override is intentionally global. In particular it must
    // also release an explicit practice cap during the intro and menus.
    const bool fullSpeedDown = m_FullSpeedKey && m_Input->IsKeyDown(m_FullSpeedKey);
    const bool saveDown = m_SaveKey && m_Input->IsKeyDown(m_SaveKey);
    const bool restoreDown = m_RestoreKey && m_Input->IsKeyDown(m_RestoreKey);
    if (fullSpeedDown && !m_FullSpeedActive)
        BeginFullSpeed();
    else if (!fullSpeedDown && m_FullSpeedActive)
        EndFullSpeed();

    if (inLevel && saveDown && !m_SaveDown)
        SaveState();
    else if (inLevel && restoreDown && !m_RestoreDown)
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

bool PracticeTools::SaveState() {
    if (!m_Context || !(m_BML->IsPlaying() || m_BML->IsPaused()))
        return false;

#ifdef __APPLE__
    const auto captureTransform = reinterpret_cast<CaptureEntityTransform>(
        ResolveEntityTransformSymbol("CKCaptureEntityTransformState"));
    const auto capture2d = reinterpret_cast<CaptureEntity2d>(
        ResolveEntityTransformSymbol("CKCaptureEntity2dState"));
    const auto captureUserBox = reinterpret_cast<CaptureEntityUserBox>(
        ResolveEntityTransformSymbol("CKCaptureEntityUserBoxState"));
    const auto captureVisibility = reinterpret_cast<CaptureEntityVisibility>(
        ResolveEntityTransformSymbol("CKCaptureEntityVisibilityState"));
    if (!captureTransform || !capture2d || !captureUserBox || !captureVisibility ||
        !ResolveEntityTransformSymbol("CKRestoreEntityVisibilityState") ||
        !ResolveEntityTransformSymbol("CKRestoreEntityUserBoxState") ||
        !ResolveEntityTransformSymbol("CKRestoreEntity2dStates")) {
        m_BML->SendIngameMessage("Native entity snapshot API unavailable");
        return false;
    }
#endif
    bool paused = false;
    if (BML::Gameplay::ReadPauseState(paused) != BML_OK) {
        m_BML->SendIngameMessage("Loader pause snapshot API unavailable");
        return false;
    }
    const auto captureStarted = std::chrono::steady_clock::now();
    AuditOpaqueRuntimeParameters(m_Context, GetLogger());
    // Only the scratch bank may be overwritten. The committed snapshot stays
    // untouched until every subsystem has captured successfully.
    struct ScheduleGuard {
        CKTimeManager *time;
        ~ScheduleGuard() { if (time) time->ResetRuntimeSchedule(); }
    } scheduleGuard{m_Time};
    const auto releaseSnapshot = [this](Snapshot *value) {
        DestroySnapshot(value);
        delete value;
    };
    using PendingSnapshot = std::unique_ptr<Snapshot, decltype(releaseSnapshot)>;
    PendingSnapshot previous(m_ReusableSnapshot.release(), releaseSnapshot);
    PendingSnapshot snapshot(new Snapshot(), releaseSnapshot);
    std::unique_ptr<Snapshot> reusable(new Snapshot());
    std::unordered_set<CKObject *> privateParameters;
    std::unordered_map<CK_ID, ObjectState *> reusableParameters;
    std::unordered_map<CK_ID, ArrayState *> reusableArrays;
    CollectSnapshotStorage(m_Snapshot.get(), privateParameters);
    CollectSnapshotStorage(previous.get(), privateParameters);
    if (previous) {
        for (ObjectState &state : previous->parameters)
            reusableParameters[state.id] = &state;
        for (ArrayState &array : previous->arrays)
            reusableArrays[array.id] = &array;
    }
    const auto failCapture = [this](const char *stage) {
        const char *requested = std::getenv("BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE");
        if (!requested || std::strcmp(requested, stage) != 0) return false;
        GetLogger()->Warn("Injected practice capture failure at %s.", stage);
        return true;
    };
    snapshot->paused = paused;
    const XObjectPointerArray &allObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::vector<CKObject *> objects(allObjects.Begin(), allObjects.End());
    std::unordered_set<CKObject *> liveObjects(objects.begin(), objects.end());
    size_t invalidArrayParameters = 0;
    CKScene *scene = m_Context->GetCurrentScene();

    if (std::getenv("BALLANCE_SNAPSHOT_AUDIT")) {
        std::map<CK_CLASSID, size_t> classCounts;
        for (CKObject *object : objects) {
            if (object && !object->IsToBeDeleted() &&
                !IsOwnedBySnapshotStorage(object, privateParameters))
                ++classCounts[object->GetClassID()];
        }
        for (const auto &entry : classCounts) {
            GetLogger()->Info("Snapshot class audit: %s(%d)=%zu.",
                              CKClassIDToString(entry.first),
                              static_cast<int>(entry.first), entry.second);
        }
    }

    for (CKObject *object : objects) {
        if (!object || object->IsToBeDeleted() ||
            IsOwnedBySnapshotStorage(object, privateParameters))
            continue;

        const CK_CLASSID cid = object->GetClassID();
        if (CKIsChildClassOf(cid, CKCID_MATERIAL) &&
            TT_SimpleShadowIsRuntimeMaterial(
                m_Context, static_cast<CKMaterial *>(object)))
            continue;

        snapshot->identities[object->GetID()] = object;
#ifdef __APPLE__
        const auto lifetime = CKGetObjectLifetimeToken(m_Context, object->GetID());
        if (!lifetime) {
            m_BML->SendIngameMessage("Object lifetime identity is unavailable");
            return false;
        }
        snapshot->lifetimeTokens[object->GetID()] = lifetime;
#endif
        snapshot->identityInfo[object->GetID()] = {
            object->GetClassID(), object->GetName() ? object->GetName() : ""};

        if (CKIsChildClassOf(cid, CKCID_BEOBJECT)) {
            auto *beObject = static_cast<CKBeObject *>(object);
            BeObjectState beState;
            beState.id = object->GetID();
            beState.priority = beObject->GetPriority();
            beState.messageWaitingCount = beObject->GetMessageWaitingCount();
            for (int script = 0; script < beObject->GetScriptCount(); ++script) {
                if (CKBehavior *behavior = beObject->GetScript(script))
                    beState.scripts.push_back(behavior->GetID());
            }
            snapshot->beObjects.push_back(std::move(beState));
            AttributeState state;
            state.id = object->GetID();
            const int attributeCount = beObject->GetAttributeCount();
            state.entries.reserve(attributeCount);
            for (int i = 0; i < attributeCount; ++i) {
                CKParameterOut *parameter =
                    beObject->GetAttributeParameterByIndex(i);
                state.entries.push_back({
                    beObject->GetAttributeType(i),
                    parameter ? parameter->GetID() : 0});
                if (parameter) {
                    PracticeTimeline::ParameterReferences references;
                    if (PracticeTimeline::CaptureParameterReferences(parameter, references) != CK_OK)
                        return false;
                    PracticeTimeline::CollectReferenceIdentities(references,
                        snapshot->attributeParameterIdentities);
                }
            }
            snapshot->attributes.push_back(std::move(state));
        }
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

        // Capture topology even for opaque values and array-owned cells whose
        // values are handled separately. Structured children are registered
        // objects too and must not propagate while their parent is copied.
        if (auto *output = CKParameterOut::Cast(object)) {
            PracticeTimeline::OutputState state;
            state.id = output->GetID();
            for (int i = 0; i < output->GetDestinationCount(); ++i) {
                auto *destination = output->GetDestination(i);
                state.destinations.push_back(destination ? destination->GetID() : 0);
            }
            snapshot->parameterOutputs.push_back(std::move(state));
        }

        if (CKIsChildClassOf(cid, CKCID_PARAMETER)) {
            auto *parameter = static_cast<CKParameter *>(object);
            if (IsOpaqueRuntimeParameter(parameter))
                continue;
            // Array-owned cells are captured below using their stable
            // row/column identity. Capturing them here too doubles both the
            // memory and CopyValue work and was the reason the original code
            // needed a special second pass in the first place.
            if (CKDataArray::Cast(parameter->GetOwner()))
            {
                snapshot->arrayCellIdentities.insert(object->GetID());
                continue;
            }
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
            if (!copy || failCapture("parameter-value") || copy->CopyValue(parameter, FALSE) != CK_OK) {
                if (copy) m_Context->DestroyObject(copy);
                m_BML->SendIngameMessage("Unable to capture parameter value");
                return false;
            }
            snapshot->parameters.push_back({object->GetID(), copy});
            if (PracticeTimeline::CaptureParameterReferences(parameter,
                    snapshot->parameters.back().references) != CK_OK) {
                GetLogger()->Warn("Typed reference capture failed: id=%u name=%s type=%d size=%d kind=%d.",
                    unsigned(parameter->GetID()), parameter->GetName() ? parameter->GetName() : "<null>",
                    parameter->GetType(), parameter->GetDataSize(), int(snapshot->parameters.back().references.kind));
                m_BML->SendIngameMessage("Unable to capture typed parameter references");
                return false;
            }
            continue;
        }

        if (CKIsChildClassOf(cid, CKCID_PARAMETERIN)) {
            auto *parameter = static_cast<CKParameterIn *>(object);
            ParameterInState state;
            state.id = object->GetID();
            state.shared = (parameter->GetObjectFlags() & CK_PARAMETERIN_SHARED) != 0;
            if (state.shared) {
                CKParameterIn *shared = parameter->GetSharedSource();
                state.source = shared ? shared->GetID() : 0;
            } else if (CKParameter *source = parameter->GetDirectSource()) {
                state.source = source->GetID();
            }
            state.enabled = parameter->IsEnabled() != FALSE;
            snapshot->parameterInputs.push_back(state);
        }

        if (CKIsChildClassOf(cid, CKCID_DATAARRAY)) {
            auto *array = static_cast<CKDataArray *>(object);
            snapshot->arrays.emplace_back();
            ArrayState &state = snapshot->arrays.back();
            state.id = object->GetID();
            state.rows = array->GetRowCount();
            state.columns = array->GetColumnCount();
            state.keyColumn = array->GetKeyColumn();
            state.columnStates.reserve(state.columns);
            for (int column = 0; column < state.columns; ++column) {
                ArrayState::ColumnState savedColumn;
                savedColumn.type = array->GetColumnType(column);
                savedColumn.parameterGuid = array->GetColumnParameterGuid(column);
                CKSTRING name = array->GetColumnName(column);
                if (name)
                    savedColumn.name = name;
                state.columnStates.push_back(std::move(savedColumn));
            }
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
                            cell.source = parameter->GetID();
                            cell.owned = parameter->GetOwner() == array;
                            if (IsOpaqueRuntimeParameter(parameter)) {
                                ++invalidArrayParameters;
                            } else if (oldArray) {
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
                            if (!IsOpaqueRuntimeParameter(parameter) && !cell.parameter)
                                cell.parameter = m_Context->CreateCKParameterOut(
                                    nullptr, parameter->GetGUID(), TRUE);
                            if (!IsOpaqueRuntimeParameter(parameter) &&
                                (!cell.parameter || failCapture("array-value") ||
                                 cell.parameter->CopyValue(parameter, FALSE) != CK_OK)) {
                                if (cell.parameter) m_Context->DestroyObject(cell.parameter);
                                cell.parameter = nullptr;
                                m_BML->SendIngameMessage("Unable to capture data-array parameter value");
                                return false;
                            }
                            if (cell.parameter && PracticeTimeline::CaptureParameterReferences(
                                    parameter, cell.references) != CK_OK) {
                                m_Context->DestroyObject(cell.parameter);
                                cell.parameter = nullptr;
                                m_BML->SendIngameMessage("Unable to capture typed array references");
                                return false;
                            }
                            if (cell.owned)
                                PracticeTimeline::CollectReferenceIdentities(cell.references,
                                    snapshot->arrayCellIdentities);
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
            continue;
        }

        if (CKIsChildClassOf(cid, CKCID_3DENTITY)) {
            auto *entity = static_cast<CK3dEntity *>(object);
            CK3dEntity *parent = entity->GetParent();
            CKMesh *currentMesh = entity->GetCurrentMesh();
            EntityState state;
            state.id = object->GetID();
            state.parent = parent ? parent->GetID() : 0;
            if (parent) {
                for (int child = 0; child < parent->GetChildrenCount(); ++child) {
                    if (parent->GetChild(child) == entity) {
                        state.siblingIndex = child;
                        break;
                    }
                }
            }
            state.currentMesh = currentMesh ? currentMesh->GetID() : 0;
            for (int i = 0; i < entity->GetMeshCount(); ++i) {
                if (CKMesh *mesh = entity->GetMesh(i))
                    state.meshes.push_back(mesh->GetID());
            }
            state.local = entity->GetLocalMatrix();
#ifdef __APPLE__
            // Preserve both local/world transforms and their cache state; a
            // matrix setter alone recomputes parent/animation state on rewind.
            if (!captureTransform(entity, &state.exactTransform, sizeof(state.exactTransform))) {
                m_BML->SendIngameMessage("Unable to capture exact entity transform");
                return false;
            }
            state.exactTransformCaptured = true;
            if (!captureUserBox(entity, &state.userBox, sizeof(state.userBox)) ||
                !captureVisibility(entity, &state.visibilityHistory, sizeof(state.visibilityHistory))) {
                m_BML->SendIngameMessage("Unable to capture entity user bounding box");
                return false;
            }
#endif
            state.flags = entity->GetFlags();
            state.moveableFlags = entity->GetMoveableFlags();
            state.zOrder = entity->GetZOrder();
            state.visible = entity->IsVisible() != FALSE;
            snapshot->entities.push_back(std::move(state));
        } else if (CKIsChildClassOf(cid, CKCID_2DENTITY)) {
            auto *entity = static_cast<CK2dEntity *>(object);
            Entity2dState state;
#ifdef __APPLE__
            if (!capture2d(entity, &state.exactLayout, sizeof(state.exactLayout))) {
                m_BML->SendIngameMessage("Unable to capture exact 2D layout");
                return false;
            }
            for (int child = 0; child < entity->GetChildrenCount(); ++child) {
                auto *item = entity->GetChild(child);
                if (!item || !liveObjects.count(item) || item->IsToBeDeleted()) {
                    m_BML->SendIngameMessage("Unable to capture 2D child identity");
                    return false;
                }
                state.children.push_back(item->GetID());
            }
#endif
            state.id = object->GetID();
            CK2dEntity *parent = entity->GetParent();
            state.parent = parent ? parent->GetID() : 0;
            if (parent) {
                for (int child = 0; child < parent->GetChildrenCount(); ++child) {
                    if (parent->GetChild(child) == entity) {
                        state.siblingIndex = child;
                        break;
                    }
                }
            }
            state.flags = entity->GetFlags();
            state.zOrder = entity->GetZOrder();
            if (CKMaterial *material = entity->GetMaterial())
                state.material = material->GetID();
            state.visible = entity->IsVisible() != FALSE;
            state.homogeneous = entity->GetHomogeneousRect(state.rect) == CK_OK;
            if (!state.homogeneous)
                entity->GetRect(state.rect);
            entity->GetSourceRect(state.sourceRect);
            if (CKIsChildClassOf(cid, CKCID_SPRITE)) {
                auto *sprite = static_cast<CKSprite *>(object);
                state.spriteSlot = sprite->GetCurrentSlot();
                snapshot->sprites.push_back({
                    object->GetID(), sprite->GetTransparentColor(),
                    sprite->IsTransparent() != FALSE});
            }
            if (CKIsChildClassOf(cid, CKCID_SPRITETEXT)) {
                auto *text = static_cast<CKSpriteText *>(object);
                SpriteTextState textState;
                textState.id = object->GetID();
                if (CKSTRING value = text->GetText())
                    textState.text = value;
                textState.textColor = text->GetTextColor();
                textState.backgroundColor = text->GetBackgroundTextColor();
                textState.alignment = text->GetAlign();
                snapshot->spriteTexts.push_back(std::move(textState));
            }
            snapshot->entities2d.push_back(state);
        }

        if (CKIsChildClassOf(cid, CKCID_TEXTURE)) {
            auto *texture = static_cast<CKTexture *>(object);
            snapshot->textures.push_back({
                object->GetID(), texture->GetCurrentSlot(),
                texture->GetTransparentColor(),
                texture->IsTransparent() != FALSE});
        }
        if (CKIsChildClassOf(cid, CKCID_SPRITE3D)) {
            auto *sprite = static_cast<CKSprite3D *>(object);
            Sprite3dState state;
            state.id = object->GetID();
            CKMaterial *material = sprite->GetMaterial();
            state.material = material ? material->GetID() : 0;
            sprite->GetSize(state.size);
            sprite->GetOffset(state.offset);
            sprite->GetUVMapping(state.uv);
            state.mode = sprite->GetMode();
            snapshot->sprites3d.push_back(state);
        }

        if (CKIsChildClassOf(cid, CKCID_CAMERA)) {
            auto *camera = static_cast<CKCamera *>(object);
            CameraState state;
            state.id = object->GetID();
            state.frontPlane = camera->GetFrontPlane();
            state.backPlane = camera->GetBackPlane();
            state.fov = camera->GetFov();
            state.projection = camera->GetProjectionType();
            state.orthographicZoom = camera->GetOrthographicZoom();
            camera->GetAspectRatio(state.aspectWidth, state.aspectHeight);
            CK3dEntity *target = camera->GetTarget();
            state.target = target ? target->GetID() : 0;
            snapshot->cameras.push_back(state);
        }
        if (CKIsChildClassOf(cid, CKCID_LIGHT)) {
            auto *light = static_cast<CKLight *>(object);
            LightState state;
            state.id = object->GetID();
            state.color = light->GetColor();
            state.constantAttenuation = light->GetConstantAttenuation();
            state.linearAttenuation = light->GetLinearAttenuation();
            state.quadraticAttenuation = light->GetQuadraticAttenuation();
            state.type = light->GetType();
            state.range = light->GetRange();
            state.hotSpot = light->GetHotSpot();
            state.fallOff = light->GetFallOff();
            state.fallOffShape = light->GetFallOffShape();
            state.power = light->GetLightPower();
            state.active = light->GetActivity() != FALSE;
            state.specular = light->GetSpecularFlag() != FALSE;
            CK3dEntity *target = light->GetTarget();
            state.target = target ? target->GetID() : 0;
            snapshot->lights.push_back(state);
        }
        if (CKIsChildClassOf(cid, CKCID_MATERIAL)) {
            auto *material = static_cast<CKMaterial *>(object);
            if (TT_SimpleShadowIsRuntimeMaterial(m_Context, material))
                continue;
            MaterialState state;
            state.id = object->GetID();
            state.power = material->GetPower();
            state.ambient = material->GetAmbient();
            state.diffuse = material->GetDiffuse();
            state.specular = material->GetSpecular();
            state.emissive = material->GetEmissive();
            for (int texture = 0; texture < 4; ++texture) {
                CKTexture *value = material->GetTexture(texture);
                state.textures[texture] = value ? value->GetID() : 0;
            }
            state.textureBlend = material->GetTextureBlendMode();
            state.textureMin = material->GetTextureMinMode();
            state.textureMag = material->GetTextureMagMode();
            state.textureAddress = material->GetTextureAddressMode();
            state.textureBorderColor = material->GetTextureBorderColor();
            state.sourceBlend = material->GetSourceBlend();
            state.destinationBlend = material->GetDestBlend();
            state.zFunction = material->GetZFunc();
            state.fillMode = material->GetFillMode();
            state.shadeMode = material->GetShadeMode();
            state.alphaFunction = material->GetAlphaFunc();
            state.alphaReference = material->GetAlphaRef();
            state.effect = material->GetEffect();
            if (auto *parameter = material->GetEffectParameter()) {
                if (PracticeTimeline::CaptureParameterReferences(
                        parameter, state.effectParameter) != CK_OK) {
                    m_BML->SendIngameMessage("Unable to capture material effect parameters");
                    return false;
                }
                PracticeTimeline::CollectReferenceIdentities(
                    state.effectParameter, snapshot->effectParameterIdentities);
            }
            state.twoSided = material->IsTwoSided() != FALSE;
            state.zWrite = material->ZWriteEnabled() != FALSE;
            state.alphaBlend = material->AlphaBlendEnabled() != FALSE;
            state.perspectiveCorrection =
                material->PerspectiveCorrectionEnabled() != FALSE;
            state.alphaTest = material->AlphaTestEnabled() != FALSE;
            snapshot->materials.push_back(state);
        }
        if (CKIsChildClassOf(cid, CKCID_MESH)) {
            auto *mesh = static_cast<CKMesh *>(object);
            MeshState state;
            state.id = object->GetID();
            state.flags = mesh->GetFlags();
            state.wrapMode = mesh->GetWrapMode();
            state.litMode = mesh->GetLitMode();
            state.transparent = mesh->IsTransparent() != FALSE;
            const int vertexCount = mesh->GetVertexCount();
            state.positions.resize(vertexCount);
            state.normals.resize(vertexCount);
            state.colors.resize(vertexCount);
            state.specularColors.resize(vertexCount);
            for (int vertex = 0; vertex < vertexCount; ++vertex) {
                mesh->GetVertexPosition(vertex, &state.positions[vertex]);
                mesh->GetVertexNormal(vertex, &state.normals[vertex]);
                state.colors[vertex] = mesh->GetVertexColor(vertex);
                state.specularColors[vertex] = mesh->GetVertexSpecularColor(vertex);
            }
            state.channels.emplace_back(); // base texture coordinate set
            state.channels[0].uvs.resize(vertexCount);
            for (int vertex = 0; vertex < vertexCount; ++vertex)
                mesh->GetVertexTextureCoordinates(vertex,
                    &state.channels[0].uvs[vertex].x,
                    &state.channels[0].uvs[vertex].y, -1);
            const int channelCount = mesh->GetChannelCount();
            std::vector<int> savedChannelIndex(channelCount, -1);
            for (int channel = 0; channel < channelCount; ++channel) {
                CKMaterial *material = mesh->GetChannelMaterial(channel);
                if (material && TT_SimpleShadowIsRuntimeMaterial(m_Context, material))
                    continue; // derived cache, rebuilt by TT Simple Shadow
                savedChannelIndex[channel] =
                    static_cast<int>(state.channels.size()) - 1;
                state.channels.emplace_back();
                MeshChannelState &savedChannel = state.channels.back();
                savedChannel.material = material ? material->GetID() : 0;
                savedChannel.flags = mesh->GetChannelFlags(channel);
                savedChannel.sourceBlend = mesh->GetChannelSourceBlend(channel);
                savedChannel.destinationBlend = mesh->GetChannelDestBlend(channel);
                savedChannel.uvs.resize(vertexCount);
                for (int vertex = 0; vertex < vertexCount; ++vertex)
                    mesh->GetVertexTextureCoordinates(
                        vertex, &savedChannel.uvs[vertex].x,
                        &savedChannel.uvs[vertex].y, channel);
            }
            const int faceCount = mesh->GetFaceCount();
            state.faces.resize(faceCount);
            for (int face = 0; face < faceCount; ++face) {
                MeshFaceState &savedFace = state.faces[face];
                mesh->GetFaceVertexIndex(face, savedFace.vertices[0],
                                         savedFace.vertices[1],
                                         savedFace.vertices[2]);
                CKMaterial *material = mesh->GetFaceMaterial(face);
                savedFace.material = material ? material->GetID() : 0;
                const CKWORD runtimeMask = mesh->GetFaceChannelMask(face);
                savedFace.channelMask = 0;
                for (int oldChannel = 0; oldChannel < channelCount; ++oldChannel) {
                    const int newChannel = savedChannelIndex[oldChannel];
                    if (newChannel >= 0 && (runtimeMask & (CKWORD(1) << oldChannel)))
                        savedFace.channelMask |= CKWORD(1) << newChannel;
                }
            }
            const int lineCount = mesh->GetLineCount();
            CKWORD *lines = mesh->GetLineIndices();
            if (lines && lineCount > 0)
                state.lines.assign(lines, lines + lineCount * 2);
            snapshot->meshes.push_back(std::move(state));
        }
    }

    if (scene) {
        snapshot->environment.ambientLight = scene->GetAmbientLight();
        snapshot->environment.fogMode = scene->GetFogMode();
        snapshot->environment.fogStart = scene->GetFogStart();
        snapshot->environment.fogEnd = scene->GetFogEnd();
        snapshot->environment.fogDensity = scene->GetFogDensity();
        snapshot->environment.fogColor = scene->GetFogColor();
        snapshot->environment.backgroundColor = scene->GetBackgroundColor();
        CKTexture *background = scene->GetBackgroundTexture();
        snapshot->environment.backgroundTexture = background ? background->GetID() : 0;
        if (CKRenderContext *render = m_Context->GetPlayerRenderContext()) {
            CKCamera *camera = render->GetAttachedCamera();
            snapshot->environment.attachedCamera = camera ? camera->GetID() : 0;
            snapshot->environment.clearBackground = render->GetClearBackground() != FALSE;
        }
        snapshot->environment.captured = true;
    }

    if (failCapture("after-values")) return false;
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
        snapshot->scheduler = behaviors->CaptureRuntimeState();
    }
    if (!snapshot->scheduler) {
        m_BML->SendIngameMessage("Unable to capture behavior scheduler state");
        return false;
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

        const auto captureMessage = [this](CKMessage *message,
                                            CK_ID lastFrameObject,
                                            RuntimeMessageState &state) {
            if (!message)
                return false;
            state.type = message->GetMsgType();
            state.sendingType = message->GetSendingType();
            CKBeObject *sender = message->GetSender();
            state.sender = sender ? sender->GetID() : 0;
            if (state.sendingType == CK_MESSAGE_BROADCAST)
                state.broadcastClass = message->GetBroadcastObjectType();
            else {
                CKObject *recipient = message->GetRecipient();
                state.recipient = recipient ? recipient->GetID() : 0;
            }
            state.lastFrameObject = lastFrameObject;
            for (int i = 0; i < message->GetParameterCount(); ++i) {
                CKParameter *parameter = message->GetParameter(i);
                if (!parameter || IsOpaqueRuntimeParameter(parameter))
                    return false; // Do not silently remove a positional message argument.
                CKParameterOut *copy = m_Context->CreateCKParameterOut(
                    nullptr, parameter->GetGUID(), TRUE);
                if (!copy || copy->CopyValue(parameter, FALSE) != CK_OK) {
                    if (copy)
                        m_Context->DestroyObject(copy);
                    return false;
                }
                state.parameters.push_back({copy});
                if (PracticeTimeline::CaptureParameterReferences(parameter,
                        state.parameters.back().references) != CK_OK) return false;
            }
            return true;
        };
        for (int i = 0; i < messages->GetRuntimePendingMessageCount(); ++i) {
            snapshot->pendingMessages.emplace_back();
            if (!captureMessage(messages->GetRuntimePendingMessage(i), 0, snapshot->pendingMessages.back())) {
                m_BML->SendIngameMessage("Unable to capture pending message parameters");
                return false;
            }
        }
        for (int i = 0; i < messages->GetRuntimeLastFrameObjectCount(); ++i) {
            CKBeObject *object = messages->GetRuntimeLastFrameObject(i);
            if (!object)
                continue;
            for (int j = 0; j < object->GetLastFrameMessageCount(); ++j) {
                snapshot->lastFrameMessages.emplace_back();
                if (!captureMessage(object->GetLastFrameMessage(j), object->GetID(), snapshot->lastFrameMessages.back())) {
                    m_BML->SendIngameMessage("Unable to capture previous-frame message parameters");
                    return false;
                }
            }
        }
    }

    if (TT_PhysicsCaptureSnapshot(m_Context, &snapshot->physics) != CK_OK) {
        m_BML->SendIngameMessage("Unable to capture physics state");
        return false;
    }
    if (failCapture("after-physics")) return false;
    if (TT_ParticleCaptureSnapshot(m_Context, &snapshot->particles) != CK_OK ||
        TT_ExtraCaptureSnapshot(m_Context, &snapshot->extras) != CK_OK ||
        TT_SimpleShadowCaptureSnapshot(
            m_Context, &snapshot->simpleShadows) != CK_OK) {
        m_BML->SendIngameMessage("Unable to capture runtime state");
        return false;
    }
#ifdef __APPLE__
    const auto captureAnimations = reinterpret_cast<int (*)(CKContext *, void **)>(
        ResolveEntityTransformSymbol("CKCaptureAnimationState"));
    snapshot->prepareAnimationRestore = reinterpret_cast<int (*)(CKContext *, void *, void **)>(
        ResolveEntityTransformSymbol("CKPrepareAnimationRestore"));
    snapshot->commitAnimationRestore = reinterpret_cast<int (*)(CKContext *, void *)>(
        ResolveEntityTransformSymbol("CKCommitAnimationRestore"));
    snapshot->destroyAnimationRestore = reinterpret_cast<void (*)(void *)>(
        ResolveEntityTransformSymbol("CKDestroyAnimationRestore"));
    snapshot->destroyAnimationState = reinterpret_cast<void (*)(void *)>(
        ResolveEntityTransformSymbol("CKDestroyAnimationState"));
    if (!captureAnimations || !snapshot->prepareAnimationRestore ||
        !snapshot->commitAnimationRestore || !snapshot->destroyAnimationRestore ||
        !snapshot->destroyAnimationState ||
        captureAnimations(m_Context, &snapshot->animationState) != CK_OK) {
        m_BML->SendIngameMessage("Unable to capture animation runtime graph");
        return false;
    }
    const auto captureCallbacks = reinterpret_cast<int (*)(CKContext *, void **)>(
        ResolveEntityTransformSymbol("CKCaptureRenderCallbacks"));
    snapshot->validateRenderCallbacks = reinterpret_cast<int (*)(CKContext *, void *)>(
        ResolveEntityTransformSymbol("CKValidateRenderCallbacks"));
    snapshot->restoreRenderCallbacks = reinterpret_cast<int (*)(CKContext *, void *)>(
        ResolveEntityTransformSymbol("CKRestoreRenderCallbacks"));
    snapshot->destroyRenderCallbacks = reinterpret_cast<void (*)(void *)>(
        ResolveEntityTransformSymbol("CKDestroyRenderCallbacks"));
    if (!captureCallbacks || !snapshot->validateRenderCallbacks ||
        !snapshot->restoreRenderCallbacks || !snapshot->destroyRenderCallbacks ||
        captureCallbacks(m_Context, &snapshot->renderCallbacks) != CK_OK) {
        m_BML->SendIngameMessage("Unable to capture renderer callbacks");
        return false;
    }
#endif
#ifdef __APPLE__
    snapshot->soundLibrary = OpenSoundSnapshotProvider();
    if (snapshot->soundLibrary) {
        auto capture = reinterpret_cast<int (*)(CKContext *, void **)>(
            dlsym(snapshot->soundLibrary, "SDL_SoundCaptureSnapshot"));
        snapshot->prepareSound = reinterpret_cast<int (*)(CKContext *, void *, void **)>(
            dlsym(snapshot->soundLibrary, "SDL_SoundPrepareRestore"));
        snapshot->commitSound = reinterpret_cast<int (*)(CKContext *, void *)>(
            dlsym(snapshot->soundLibrary, "SDL_SoundCommitRestore"));
        snapshot->destroySoundRestore = reinterpret_cast<void (*)(void *)>(
            dlsym(snapshot->soundLibrary, "SDL_SoundDestroyRestore"));
        snapshot->destroySound = reinterpret_cast<void (*)(void *)>(
            dlsym(snapshot->soundLibrary, "SDL_SoundDestroySnapshot"));
        if (!capture || !snapshot->prepareSound || !snapshot->commitSound ||
            !snapshot->destroySoundRestore || !snapshot->destroySound ||
            capture(m_Context, &snapshot->sound) != CK_OK) {
            m_BML->SendIngameMessage("Unable to capture complete sound runtime state");
            return false;
        }
    } else {
        m_BML->SendIngameMessage("Native sound snapshot API unavailable");
        return false;
    }
#endif
    BML::Speedrun::TimerSnapshot timer{};
    snapshot->timerCaptured = BML::Speedrun::ReadTimerSnapshot(timer) == BML_OK;
    if (!snapshot->timerCaptured) {
        m_BML->SendIngameMessage("Unable to capture precise SR timer state");
        return false;
    }
    snapshot->timerElapsed = timer.ElapsedTime;
    snapshot->timerRunning = timer.Running != 0;
#ifdef __APPLE__
    // This optional legacy module owns a second, independent split timer.
    // Keep its code loaded for the checkpoint lifetime; the provider validates
    // the live mod instance and level epoch before any world restoration.
    snapshot->segmentLibrary = OpenLegacySegmentProvider();
    if (snapshot->segmentLibrary) {
        const auto capture = reinterpret_cast<int (*)(CKContext *, LegacySegmentCheckpointV1 *)>(
            dlsym(snapshot->segmentLibrary, "BML_LegacySegmentCapture"));
        snapshot->validateSegment = reinterpret_cast<int (*)(CKContext *, const LegacySegmentCheckpointV1 *)>(
            dlsym(snapshot->segmentLibrary, "BML_LegacySegmentValidate"));
        snapshot->restoreSegment = reinterpret_cast<int (*)(CKContext *, const LegacySegmentCheckpointV1 *)>(
            dlsym(snapshot->segmentLibrary, "BML_LegacySegmentRestore"));
        if (!capture || !snapshot->validateSegment || !snapshot->restoreSegment ||
            !capture(m_Context, &snapshot->segment)) {
            m_BML->SendIngameMessage("Unable to capture legacy split timer state");
            return false;
        }
        GetLogger()->Info("Segment capture: sector=%d elapsed=%a counting=%d duty=%lld.",
            snapshot->segment.sector, snapshot->segment.elapsed,
            int(snapshot->segment.counting), snapshot->segment.dutyCursor);
    }
#endif
    snapshot->scoreCaptured =
        ReadArrayInt(m_BML, "Energy", 0, snapshot->energyPoints) &&
        ReadArrayInt(m_BML, "CurrentLevel", 5, snapshot->levelPoints);
    snapshot->randomState = CKGetRandomState();
    if (m_Time) {
        snapshot->absoluteTime = m_Time->GetAbsoluteTime();
        snapshot->lastDeltaTime = m_Time->GetLastDeltaTime();
        snapshot->lastDeltaTimeFree = m_Time->GetLastDeltaTimeFree();
        snapshot->mainTick = m_Time->GetMainTickCount();
        snapshot->clockCaptured = true;
    }
#if defined(__APPLE__)
    snapshot->toolboxTimeCaptured = TT_TimeCaptureRuntimeStateV2(
        m_Context, &snapshot->toolboxFixedTicks, &snapshot->toolboxTime,
        &snapshot->toolboxTiming, &snapshot->toolboxRecordedClock) == CK_OK;
    if (!snapshot->toolboxTimeCaptured) {
        m_BML->SendIngameMessage("Unable to capture toolbox clock state");
        return false;
    }
#elif defined(__linux__)
    if (CaptureToolboxTime capture = ResolveCaptureToolboxTime()) {
        snapshot->toolboxTimeCaptured = capture(
            m_Context, &snapshot->toolboxFixedTicks, &snapshot->toolboxTime,
            &snapshot->toolboxTiming, &snapshot->toolboxRecordedClock) == CK_OK;
    }
#endif

    if (failCapture("before-commit")) return false;
    if (m_Snapshot) {
        // Retain just the expensive CK value clones as the next scratch bank;
        // release old physics, renderer, scheduler and timer snapshots now.
        reusable->parameters.swap(m_Snapshot->parameters);
        reusable->arrays.swap(m_Snapshot->arrays);
        DestroySnapshot(m_Snapshot.get());
    }
    m_ReusableSnapshot = std::move(reusable);
    m_Snapshot.reset(snapshot.release());
    // Include disposal of unused scratch values in capture latency, rather
    // than reporting completion before their deferred RAII cleanup.
    previous.reset();
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
    GetLogger()->Info("Snapshot pause audit: paused=%d.", int(m_Snapshot->paused));
    m_BML->SendIngameMessage("Practice state saved");
    return true;
}

bool PracticeTools::CaptureForAutomation() {
    if (!m_Context || !(m_BML->IsPlaying() || m_BML->IsPaused()))
        return false;
    return SaveState();
}

bool PracticeTools::RestoreState() {
    if (!m_Context || !(m_BML->IsPlaying() || m_BML->IsPaused()))
        return false;
    if (!m_Snapshot) {
        m_BML->SendIngameMessage("No practice state has been saved");
        return false;
    }

#ifdef __APPLE__
    // Validate all ordinary identities before any semantic restorer may touch
    // saved pointers or mutate the world. Array cells and attribute parameters
    // are reconstructed by their owners below. Other destroyed objects still
    // require rehydration; silently skipping them is not a successful restore.
    for (const auto &saved : m_Snapshot->identities) {
        if (m_Snapshot->arrayCellIdentities.count(saved.first) ||
            m_Snapshot->attributeParameterIdentities.count(saved.first) ||
            m_Snapshot->effectParameterIdentities.count(saved.first)) continue;
        if (!PracticeTimeline::MatchesSavedObject(m_Context, saved.first,
                m_Snapshot->identities, m_Snapshot->lifetimeTokens)) {
            GetLogger()->Warn("Saved object lifetime changed: id=%u.", unsigned(saved.first));
            m_BML->SendIngameMessage("Saved object was destroyed; restore cannot reconstruct it yet");
            return false;
        }
    }
#endif

    bool currentPaused = false;
    if (BML::Gameplay::ReadPauseState(currentPaused) != BML_OK) {
        m_BML->SendIngameMessage("Loader pause snapshot API unavailable");
        return false;
    }

    BML::Speedrun::TimerSnapshot currentTimer{};
    if (!m_Snapshot->timerCaptured || BML::Speedrun::ReadTimerSnapshot(currentTimer) != BML_OK) {
        m_BML->SendIngameMessage("Precise SR timer state is unavailable");
        return false;
    }

    CKBehaviorManager *scheduler = m_Context->GetBehaviorManager();
#ifdef __APPLE__
    if (m_Snapshot->segmentLibrary && (!m_Snapshot->validateSegment ||
        !m_Snapshot->validateSegment(m_Context, &m_Snapshot->segment))) {
        m_BML->SendIngameMessage("Saved legacy split timer belongs to an inactive level or mod");
        return false;
    }
#endif
    if (!scheduler || scheduler->ValidateRuntimeState(m_Snapshot->scheduler) != CK_OK) {
        m_BML->SendIngameMessage("Saved behavior scheduler state is no longer valid");
        return false;
    }

    if (TT_ParticleValidateSnapshot(m_Context, m_Snapshot->particles) != CK_OK) {
        m_BML->SendIngameMessage("Saved particle owners are no longer valid");
        return false;
    }
#ifdef __APPLE__
    if (!m_Snapshot->renderCallbacks || !m_Snapshot->validateRenderCallbacks ||
        m_Snapshot->validateRenderCallbacks(m_Context, m_Snapshot->renderCallbacks) != CK_OK) {
        m_BML->SendIngameMessage("Saved renderer callback owners are no longer valid");
        return false;
    }
#endif

#ifdef __APPLE__
    void *preparedAnimations = nullptr;
    if (!m_Snapshot->animationState || !m_Snapshot->prepareAnimationRestore ||
        m_Snapshot->prepareAnimationRestore(m_Context, m_Snapshot->animationState,
            &preparedAnimations) != CK_OK) {
        m_BML->SendIngameMessage("Unable to prepare saved animation runtime graph");
        return false;
    }
    std::unique_ptr<void, void (*)(void *)> animationRestore(
        preparedAnimations, m_Snapshot->destroyAnimationRestore);
    void *preparedSound = nullptr;
    if (!m_Snapshot->sound || !m_Snapshot->prepareSound ||
        m_Snapshot->prepareSound(m_Context, m_Snapshot->sound, &preparedSound) != CK_OK) {
        m_BML->SendIngameMessage("Unable to prepare saved sound runtime state");
        return false;
    }
    std::unique_ptr<void, void (*)(void *)> soundRestore(
        preparedSound, m_Snapshot->destroySoundRestore);
#endif
    // Allocate every replacement Extra chain before deleting timeline objects
    // or restoring CK values. Failed preparation leaves the live world intact.
    TT_ExtraRestoreHandle preparedExtras = nullptr;
    if (TT_ExtraPrepareRestore(m_Context, m_Snapshot->extras, &preparedExtras) != CK_OK) {
        m_BML->SendIngameMessage("Unable to prepare saved bonus-ball state");
        return false;
    }
    std::unique_ptr<void, decltype(&TT_ExtraDestroyRestore)> extraRestore(
        preparedExtras, &TT_ExtraDestroyRestore);

    int scoreBefore = 0;
    ReadArrayInt(m_BML, "Energy", 0, scoreBefore);
    const XObjectPointerArray &restoreObjects =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::unordered_set<CKObject *> restoreLiveObjects(
        restoreObjects.Begin(), restoreObjects.End());
    std::unordered_set<CK_ID> staleObjectIds;
    PracticeTimeline::RebuiltParameters rebuiltParameters;
    const auto resolve = [this, &staleObjectIds, &rebuiltParameters](CK_ID id) -> CKObject * {
        const auto rebuilt = rebuiltParameters.find(id);
        if (rebuilt != rebuiltParameters.end()) return rebuilt->second;
        CKObject *object = m_Context->GetObject(id);
#ifdef __APPLE__
        if (PracticeTimeline::MatchesSavedObject(m_Context, id,
                m_Snapshot->identities, m_Snapshot->lifetimeTokens)) return object;
#else
        auto identity = m_Snapshot->identities.find(id);
        if (identity != m_Snapshot->identities.end() && identity->second == object)
            return object;
#endif
        if (id)
            staleObjectIds.insert(id);
        return nullptr;
    };

    // Audit CK object lifetime independently from the fields touched below.
    // A missing ordinary object means the abandoned timeline destroyed an
    // object that a value-only rewind cannot recreate. Array cells are the one
    // intentional exception: their topology is reconstructed semantically.
    size_t missingSnapshotObjects = 0;
    const bool auditSnapshot = std::getenv("BALLANCE_SNAPSHOT_AUDIT") != nullptr;
    for (const auto &saved : m_Snapshot->identities) {
        if (m_Snapshot->arrayCellIdentities.count(saved.first) ||
            m_Snapshot->attributeParameterIdentities.count(saved.first) ||
            m_Snapshot->effectParameterIdentities.count(saved.first))
            continue;
        if (m_Context->GetObject(saved.first) != saved.second) {
            ++missingSnapshotObjects;
            if (auditSnapshot && missingSnapshotObjects <= 24) {
                const auto info = m_Snapshot->identityInfo.find(saved.first);
                GetLogger()->Info(
                    "Snapshot lifetime audit: missing id=%u class=%s(%d) name=%s.",
                    static_cast<unsigned>(saved.first),
                    info != m_Snapshot->identityInfo.end()
                        ? CKClassIDToString(info->second.classId) : "<unknown>",
                    info != m_Snapshot->identityInfo.end()
                        ? static_cast<int>(info->second.classId) : -1,
                    info != m_Snapshot->identityInfo.end() && !info->second.name.empty()
                        ? info->second.name.c_str() : "<unnamed>");
            }
        }
    }
    size_t newTimelineObjects = 0;
    std::vector<std::pair<CK_ID, CKObject *>> newTimelineIdentities;
    PracticeTimeline::Identities newTimelineOwners;
    std::unordered_set<CKObject *> snapshotStorageObjects;
    CollectSnapshotStorage(m_Snapshot.get(), snapshotStorageObjects);
    CollectSnapshotStorage(m_ReusableSnapshot.get(), snapshotStorageObjects);
    // Existing replacements belong to semantic owner slots too, including
    // structure members whose CK owner pointer is not set by CKStructCreator.
    std::unordered_set<CKObject *> semanticParameters;
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        if (!array) continue;
        for (int row = 0; row < array->GetRowCount(); ++row)
            for (int col = 0; col < array->GetColumnCount(); ++col) {
                if (array->GetColumnType(col) != CKARRAYTYPE_PARAMETER) continue;
                auto *element = array->GetElement(row, col);
                auto *parameter = element ? reinterpret_cast<CKParameter *>(*element) : nullptr;
                if (parameter && restoreLiveObjects.count(parameter) && parameter->GetOwner() == array)
                    CollectSnapshotParameterStorage(m_Context, parameter, semanticParameters);
            }
    }
    for (const AttributeState &state : m_Snapshot->attributes) {
        auto *owner = CKBeObject::Cast(resolve(state.id));
        if (owner) for (const auto &entry : state.entries)
            CollectSnapshotParameterStorage(m_Context,
                owner->GetAttributeParameter(entry.type), semanticParameters);
    }
    for (const MaterialState &state : m_Snapshot->materials) {
        auto *material = CKMaterial::Cast(resolve(state.id));
        if (material)
            CollectSnapshotParameterStorage(m_Context,
                material->GetEffectParameter(), semanticParameters);
    }
    for (CKObject *object : restoreObjects) {
        if (!object || object->IsToBeDeleted())
            continue;
        if (CKMaterial *material = CKMaterial::Cast(object)) {
            if (TT_SimpleShadowIsRuntimeMaterial(m_Context, material))
                continue;
        }
#ifdef __APPLE__
        if (PracticeTimeline::MatchesSavedObject(m_Context, object->GetID(),
                m_Snapshot->identities, m_Snapshot->lifetimeTokens)) continue;
#else
        auto saved = m_Snapshot->identities.find(object->GetID());
        if (saved != m_Snapshot->identities.end() && saved->second == object)
            continue;
#endif
        // Row reconstruction legitimately creates fresh, array-owned
        // CKParameterOut instances. They are restored by row/column below.
        auto *parameter = CKParameter::Cast(object);
        if (semanticParameters.count(object) ||
            (parameter && CKDataArray::Cast(parameter->GetOwner())))
            continue;
        if (!IsOwnedBySnapshotStorage(object, snapshotStorageObjects)) {
            ++newTimelineObjects;
            newTimelineOwners.emplace(object->GetID(), object);
            if (auditSnapshot) newTimelineIdentities.emplace_back(object->GetID(), object);
            if (auditSnapshot && newTimelineObjects <= 24) {
                GetLogger()->Info(
                    "Snapshot lifetime audit: new id=%u class=%s(%d) name=%s.",
                    static_cast<unsigned>(object->GetID()),
                    CKClassIDToString(object->GetClassID()),
                    static_cast<int>(object->GetClassID()),
                    object->GetName() ? object->GetName() : "<unnamed>");
            }
        }
    }

    AuditTimelineReferences(m_Context, GetLogger(), newTimelineIdentities, "before");

    std::size_t removedTimelineObjects = 0;
    const CKERROR deletion = PracticeTimeline::DeleteNewObjects(
        m_Context, newTimelineOwners, removedTimelineObjects);
    if (deletion != CK_OK) {
        GetLogger()->Warn("Timeline object rollback failed (%d), removed=%zu/%zu.",
                          deletion, removedTimelineObjects, newTimelineOwners.size());
        m_BML->SendIngameMessage("Unable to reconcile saved object lifetimes");
        return false;
    }
    GetLogger()->Info("Timeline object rollback committed: removed=%zu.", removedTimelineObjects);

    // Restore array topology before values. Gameplay arrays are queues and
    // inventories: consuming a bonus or losing a ball can insert/remove rows,
    // so requiring the current row count to match silently preserves the
    // abandoned timeline. AddRow creates correctly owned parameter cells.
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        if (!array)
            continue;
        const CKERROR shapeResult = PracticeTimeline::RestoreArrayShape(array, state);
        if (shapeResult != CK_OK) {
            GetLogger()->Warn("Array topology restore failed: id=%u error=%d.",
                              unsigned(state.id), int(shapeResult));
            m_BML->SendIngameMessage("Unable to reconstruct saved data array");
            return false;
        }
    }

    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        if (PracticeTimeline::BindArrayParameters(m_Context, array, state,
                                                  rebuiltParameters) != CK_OK) {
            m_BML->SendIngameMessage("Unable to rebind saved array parameters");
            return false;
        }
    }

    for (const BeObjectState &state : m_Snapshot->beObjects) {
        auto *object = CKBeObject::Cast(resolve(state.id));
        if (!object)
            continue;
        object->SetPriority(state.priority);
        object->SetMessageWaitingCount(state.messageWaitingCount);

        // Script ownership is mutable (Attach Script and mods use it) and is
        // part of behavior topology.  Avoid detaching the common unchanged
        // case because detach callbacks own native caches such as TT Sky.
        bool scriptsMatch = object->GetScriptCount() ==
                            static_cast<int>(state.scripts.size());
        for (int i = 0; scriptsMatch && i < object->GetScriptCount(); ++i) {
            CKBehavior *script = object->GetScript(i);
            scriptsMatch = script && script->GetID() == state.scripts[i];
        }
        if (!scriptsMatch) {
            std::unordered_set<CK_ID> wanted(state.scripts.begin(),
                                             state.scripts.end());
            for (int i = object->GetScriptCount() - 1; i >= 0; --i) {
                CKBehavior *script = object->GetScript(i);
                if (script && !wanted.count(script->GetID()))
                    object->RemoveScript(i);
            }
            for (CK_ID scriptId : state.scripts) {
                auto *script = CKBehavior::Cast(resolve(scriptId));
                if (!script || script->GetOwner() == object)
                    continue;
                if (CKBeObject *currentOwner = script->GetOwner())
                    currentOwner->RemoveScript(scriptId);
                object->AddScript(script);
            }
        }
    }


    // Attributes are structural gameplay state (floor/collision membership,
    // checkpoints and script tags), not just parameter values. Rebuild their
    // membership before copying parameter values. If the abandoned timeline
    // removed an attribute, SetAttribute creates a fresh owned parameter and
    // the saved value is copied into that new identity below.
    for (const AttributeState &state : m_Snapshot->attributes) {
        auto *object = CKBeObject::Cast(resolve(state.id));
        if (!object)
            continue;
        std::unordered_set<CKAttributeType> wanted;
        wanted.reserve(state.entries.size());
        for (const AttributeState::Entry &entry : state.entries)
            wanted.insert(entry.type);
        for (int i = object->GetAttributeCount() - 1; i >= 0; --i) {
            const CKAttributeType type = object->GetAttributeType(i);
            if (!wanted.count(type))
                object->RemoveAttribute(type);
        }
        for (const AttributeState::Entry &entry : state.entries) {
            if (!object->HasAttribute(entry.type))
                object->SetAttribute(entry.type);
            CKParameterOut *destination =
                object->GetAttributeParameter(entry.type);
            if (entry.parameter && PracticeTimeline::BindRebuiltParameter(
                    m_Context, rebuiltParameters, entry.parameter, destination) != CK_OK) {
                m_BML->SendIngameMessage("Unable to rebind saved attribute parameters");
                return false;
            }
            // Copy the value with all output edges suspended below, together
            // with the other general parameters. Binding must happen first.
        }
    }
    // Recreate material-owned effects before parameter wiring or value copies.
    // SetEffect can change the type, destroy children, and initialize defaults;
    // doing it later would erase the restored values or reject their old IDs.
    for (const MaterialState &state : m_Snapshot->materials) {
        auto *material = CKMaterial::Cast(resolve(state.id));
        if (!material) return false;
        if (material->GetEffect() != state.effect)
            material->SetEffect(state.effect);
        auto *parameter = material->GetEffectParameter();
        if (state.effectParameter.id) {
            if (PracticeTimeline::BindParameterReferenceTree(m_Context,
                    state.effectParameter, parameter, rebuiltParameters) != CK_OK) {
                m_BML->SendIngameMessage("Unable to rebind material effect parameters");
                return false;
            }
        } else if (parameter) {
            m_BML->SendIngameMessage("Saved material has no effect parameter");
            return false;
        }
    }

    // Recreated structure members have new IDs just like their root cells.
    // Bind them before input/output topology and before any payload references.
    for (const auto &state : m_Snapshot->arrays) {
        for (const auto &cell : state.cells) {
            if (!cell.owned || !cell.parameter) continue;
            if (PracticeTimeline::BindParameterReferenceTree(m_Context, cell.references,
                    CKParameter::Cast(resolve(cell.source)), rebuiltParameters) != CK_OK) {
                m_BML->SendIngameMessage("Unable to bind structured array members");
                return false;
            }
        }
    }
    for (const auto &state : m_Snapshot->parameters) {
        if (!m_Snapshot->attributeParameterIdentities.count(state.id)) continue;
        // Children are bound recursively from the root, whose attribute has
        // already been recreated. The capture list is not parent-first.
        auto found = rebuiltParameters.find(state.id);
        if (found == rebuiltParameters.end()) continue;
        if (PracticeTimeline::BindParameterReferenceTree(m_Context, state.references,
                found->second, rebuiltParameters) != CK_OK) {
            m_BML->SendIngameMessage("Unable to bind structured attribute members");
            return false;
        }
    }
    // Shortcuts may reference another array or a reconstructed attribute.
    // Restore them only after every semantic source has been rebound.
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        if (PracticeTimeline::RestoreArrayShortcuts(array, state, resolve) != CK_OK) {
            m_BML->SendIngameMessage("Unable to restore saved array shortcuts");
            return false;
        }
    }

    // Parameter input wiring can be changed by graph-editing building blocks
    // and mods. It is topology, while the source ParameterOut value is restored
    // separately below.
    if (PracticeTimeline::RestoreInputTopology(m_Snapshot->parameterInputs, resolve) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore saved input parameter graph");
        return false;
    }

    const XObjectPointerArray &objectsAfterArrayResize =
        m_Context->GetObjectListByType(CKCID_OBJECT, TRUE);
    restoreLiveObjects.clear();
    restoreLiveObjects.insert(objectsAfterArrayResize.Begin(),
                              objectsAfterArrayResize.End());

    PracticeTimeline::OutputTopologyRestore outputGraph;
    if (outputGraph.Prepare(m_Snapshot->parameterOutputs, resolve) != CK_OK) {
        m_BML->SendIngameMessage("Unable to resolve saved output parameter graph");
        return false;
    }
    outputGraph.Suspend();

    // Restore values without loading whole CKStateChunks. Whole-object loads
    // rewrite behavior/parameter wiring, and CKDataArray::Load destroys owned
    // parameters; both operations are inappropriate for a repeatable runtime
    // rewind and were responsible for lost controls and the second-restore
    // crash.
    for (const ArrayState &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        if (!array || array->GetRowCount() != state.rows ||
            array->GetColumnCount() != state.columns) {
            GetLogger()->Warn("Skipped structurally changed data array %u (%s) during restore.",
                              state.id, array && array->GetName()
                                  ? array->GetName() : "<unnamed>");
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

    // CK-managed saved copies may have had referenced IDs nulled by object
    // deletion. Restore immutable typed references after every value copy.
    for (const auto &state : m_Snapshot->arrays) {
        auto *array = CKDataArray::Cast(resolve(state.id));
        for (const auto &cell : state.cells) {
            if (cell.parameter) {
                auto *element = array ? array->GetElement(cell.row, cell.column) : nullptr;
                auto *parameter = element ? reinterpret_cast<CKParameter *>(*element) : nullptr;
                if (PracticeTimeline::RestoreParameterReferences(parameter, cell.references, resolve) != CK_OK) {
                    m_BML->SendIngameMessage("Unable to restore typed array references");
                    return false;
                }
            } else if (cell.type == CKARRAYTYPE_OBJECT && cell.scalar) {
                auto *object = resolve(cell.scalar);
                if (!array || !object) return false;
                CK_ID id = object->GetID();
                array->SetElementValue(cell.row, cell.column, &id);
            }
        }
    }
    for (const auto &state : m_Snapshot->parameters) {
        if (PracticeTimeline::RestoreParameterReferences(
                CKParameter::Cast(resolve(state.id)), state.references, resolve) != CK_OK) {
            GetLogger()->Warn("Typed reference restore failed: id=%u kind=%d.", unsigned(state.id), int(state.references.kind));
            m_BML->SendIngameMessage("Unable to restore typed parameter references");
            return false;
        }
    }

    if (outputGraph.Commit() != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore saved output parameter graph");
        return false;
    }

    int scoreAfterValues = 0;
    ReadArrayInt(m_BML, "Energy", 0, scoreAfterValues);

    // Render objects carry mutable state outside the behavior graph. Restore
    // materials before meshes so face/channel bindings observe their saved
    // appearance, then restore all mesh streams and topology atomically.
    for (const MaterialState &state : m_Snapshot->materials) {
        auto *material = CKMaterial::Cast(resolve(state.id));
        if (!material)
            continue;
        material->SetPower(state.power);
        material->SetAmbient(state.ambient);
        material->SetDiffuse(state.diffuse);
        material->SetSpecular(state.specular);
        material->SetEmissive(state.emissive);
        for (int texture = 0; texture < 4; ++texture)
            material->SetTexture(texture, CKTexture::Cast(
                state.textures[texture] ? resolve(state.textures[texture]) : nullptr));
        material->SetTextureBlendMode(state.textureBlend);
        material->SetTextureMinMode(state.textureMin);
        material->SetTextureMagMode(state.textureMag);
        material->SetTextureAddressMode(state.textureAddress);
        material->SetTextureBorderColor(state.textureBorderColor);
        material->SetSourceBlend(state.sourceBlend);
        material->SetDestBlend(state.destinationBlend);
        material->SetTwoSided(state.twoSided ? TRUE : FALSE);
        material->EnableZWrite(state.zWrite ? TRUE : FALSE);
        material->EnableAlphaBlend(state.alphaBlend ? TRUE : FALSE);
        material->SetZFunc(state.zFunction);
        material->EnablePerspectiveCorrection(
            state.perspectiveCorrection ? TRUE : FALSE);
        material->SetFillMode(state.fillMode);
        material->SetShadeMode(state.shadeMode);
        material->EnableAlphaTest(state.alphaTest ? TRUE : FALSE);
        material->SetAlphaFunc(state.alphaFunction);
        material->SetAlphaRef(state.alphaReference);
        // Effect type/owner topology was restored before parameter values.
    }

    // A multi-slot CKTexture has its own animation cursor. Restoring only the
    // sprite slot leaves material textures on the abandoned timeline.
    for (const TextureState &state : m_Snapshot->textures) {
        auto *texture = CKTexture::Cast(resolve(state.id));
        if (texture && state.currentSlot >= 0 &&
            state.currentSlot < texture->GetSlotCount()) {
            texture->SetCurrentSlot(state.currentSlot);
            texture->SetTransparentColor(state.transparentColor);
            texture->SetTransparent(state.transparent ? TRUE : FALSE);
        }
    }

    for (const MeshState &state : m_Snapshot->meshes) {
        auto *mesh = CKMesh::Cast(resolve(state.id));
        if (!mesh)
            continue;
        const int vertexCount = static_cast<int>(state.positions.size());
        if (mesh->GetVertexCount() != vertexCount &&
            !mesh->SetVertexCount(vertexCount))
            continue;
        // Channel order is topology. Rebuild it exactly; implementation-owned
        // TT Simple Shadow channels were deliberately omitted and are derived
        // again by its pre-render callback.
        while (mesh->GetChannelCount() > 0)
            mesh->RemoveChannel(mesh->GetChannelCount() - 1);
        for (size_t slot = 1; slot < state.channels.size(); ++slot) {
            const MeshChannelState &saved = state.channels[slot];
            mesh->AddChannel(CKMaterial::Cast(
                saved.material ? resolve(saved.material) : nullptr), FALSE);
        }
        mesh->SetFlags(state.flags);
        mesh->SetWrapMode(state.wrapMode);
        mesh->SetLitMode(state.litMode);
        mesh->SetTransparent(state.transparent ? TRUE : FALSE);
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            mesh->SetVertexPosition(vertex,
                const_cast<VxVector *>(&state.positions[vertex]));
            if (vertex < static_cast<int>(state.normals.size()))
                mesh->SetVertexNormal(vertex,
                    const_cast<VxVector *>(&state.normals[vertex]));
            if (vertex < static_cast<int>(state.colors.size()))
                mesh->SetVertexColor(vertex, state.colors[vertex]);
            if (vertex < static_cast<int>(state.specularColors.size()))
                mesh->SetVertexSpecularColor(vertex, state.specularColors[vertex]);
            for (int slot = 0; slot < static_cast<int>(state.channels.size()); ++slot) {
                if (vertex < static_cast<int>(state.channels[slot].uvs.size())) {
                    const Vx2DVector &uv = state.channels[slot].uvs[vertex];
                    mesh->SetVertexTextureCoordinates(vertex, uv.x, uv.y, slot - 1);
                }
            }
        }
        for (int channel = 0; channel < mesh->GetChannelCount(); ++channel) {
            const MeshChannelState &saved = state.channels[channel + 1];
            mesh->SetChannelMaterial(channel, CKMaterial::Cast(
                saved.material ? resolve(saved.material) : nullptr));
            mesh->SetChannelFlags(channel, saved.flags);
            mesh->SetChannelSourceBlend(channel, saved.sourceBlend);
            mesh->SetChannelDestBlend(channel, saved.destinationBlend);
        }
        const int faceCount = static_cast<int>(state.faces.size());
        if (mesh->GetFaceCount() != faceCount)
            mesh->SetFaceCount(faceCount);
        for (int face = 0; face < faceCount; ++face) {
            const MeshFaceState &saved = state.faces[face];
            mesh->SetFaceVertexIndex(face, saved.vertices[0],
                                     saved.vertices[1], saved.vertices[2]);
            mesh->SetFaceMaterial(face, CKMaterial::Cast(
                saved.material ? resolve(saved.material) : nullptr));
            mesh->SetFaceChannelMask(face, saved.channelMask);
        }
        const int lineCount = static_cast<int>(state.lines.size() / 2);
        if (mesh->GetLineCount() != lineCount)
            mesh->SetLineCount(lineCount);
        for (int line = 0; line < lineCount; ++line)
            mesh->SetLine(line, state.lines[line * 2], state.lines[line * 2 + 1]);
        mesh->VertexMove();
        mesh->NormalChanged();
        mesh->ColorChanged();
        mesh->UVChanged();
    }

    // Scripts reparent the camera rig, checkpoint markers and collectible
    // effects. A saved local matrix only has meaning relative to its saved
    // parent. Detach first to avoid transient cycles, rebuild the hierarchy,
    // then restore matrices.
    size_t changed3dParents = 0;
    std::vector<const EntityState *> ordered3dHierarchy;
    ordered3dHierarchy.reserve(m_Snapshot->entities.size());
    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(resolve(state.id));
        auto *parent = CK3dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (!entity)
            continue;
        int currentSiblingIndex = -1;
        if (entity->GetParent()) {
            CK3dEntity *currentParent = entity->GetParent();
            for (int child = 0; child < currentParent->GetChildrenCount(); ++child) {
                if (currentParent->GetChild(child) == entity) {
                    currentSiblingIndex = child;
                    break;
                }
            }
        }
        if (entity->GetParent() != parent ||
            (parent && currentSiblingIndex != state.siblingIndex))
            ++changed3dParents;
        ordered3dHierarchy.push_back(&state);
        if (entity->GetParent())
            entity->SetParent(nullptr, TRUE);
    }
    std::stable_sort(ordered3dHierarchy.begin(), ordered3dHierarchy.end(),
        [](const EntityState *left, const EntityState *right) {
            if (left->parent != right->parent)
                return left->parent < right->parent;
            return left->siblingIndex < right->siblingIndex;
        });
    for (const EntityState *state : ordered3dHierarchy) {
        auto *entity = CK3dEntity::Cast(resolve(state->id));
        auto *parent = CK3dEntity::Cast(
            state->parent ? resolve(state->parent) : nullptr);
        if (entity && parent)
            entity->SetParent(parent, TRUE);
    }
    size_t final3dHierarchyMismatches = 0;
    for (const EntityState &state : m_Snapshot->entities) {
        auto *entity = CK3dEntity::Cast(resolve(state.id));
        auto *parent = CK3dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (!entity || entity->GetParent() != parent) {
            if (entity)
                ++final3dHierarchyMismatches;
            continue;
        }
        if (parent && (state.siblingIndex < 0 ||
                       state.siblingIndex >= parent->GetChildrenCount() ||
                       parent->GetChild(state.siblingIndex) != entity))
            ++final3dHierarchyMismatches;
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
        entity->SetFlags(state.flags);
        // Preserve renderer-owned visibility/culling cache bits. They describe
        // the current output frame, not gameplay state. Restore only the stable
        // author-controlled moveable flags.
        constexpr CKDWORD kTransientMoveableFlags =
            VX_MOVEABLE_VISIBLE | VX_MOVEABLE_UPTODATE |
            VX_MOVEABLE_USERBOX | VX_MOVEABLE_EXTENTSUPTODATE |
            VX_MOVEABLE_BOXVALID | VX_MOVEABLE_HASMOVED |
            VX_MOVEABLE_INVERSEWORLDMATVALID |
            VX_MOVEABLE_HIERARCHICALHIDE |
            VX_MOVEABLE_CHARACTERRENDERED | VX_MOVEABLE_RESERVED2;
        const CKDWORD currentMoveable = entity->GetMoveableFlags();
        entity->SetMoveableFlags(
            (state.moveableFlags & ~kTransientMoveableFlags) |
            (currentMoveable & kTransientMoveableFlags));
        std::vector<CKMesh *> currentMeshes;
        for (int i = 0; i < entity->GetMeshCount(); ++i) {
            if (CKMesh *mesh = entity->GetMesh(i))
                currentMeshes.push_back(mesh);
        }
        bool meshOrderMatches = currentMeshes.size() == state.meshes.size();
        for (size_t i = 0; meshOrderMatches && i < state.meshes.size(); ++i)
            meshOrderMatches = currentMeshes[i] &&
                currentMeshes[i]->GetID() == state.meshes[i];
        if (!meshOrderMatches) {
            // Mesh position is the LOD index, so set equality is insufficient.
            // Rebuild only when needed to preserve the exact saved ordering.
            for (CKMesh *mesh : currentMeshes)
                entity->RemoveMesh(mesh);
            for (CK_ID meshId : state.meshes) {
                if (CKMesh *mesh = CKMesh::Cast(resolve(meshId)))
                    entity->AddMesh(mesh);
            }
        }
        entity->SetCurrentMesh(CKMesh::Cast(
            state.currentMesh ? resolve(state.currentMesh) : nullptr), FALSE);
        entity->SetZOrder(state.zOrder);
        entity->Show(state.visible ? CKSHOW : CKHIDE);
    }


    for (const Sprite3dState &state : m_Snapshot->sprites3d) {
        auto *sprite = CKSprite3D::Cast(resolve(state.id));
        if (!sprite)
            continue;
        sprite->SetMaterial(CKMaterial::Cast(
            state.material ? resolve(state.material) : nullptr));
        Vx2DVector size = state.size;
        Vx2DVector offset = state.offset;
        VxRect uv = state.uv;
        sprite->SetSize(size);
        sprite->SetOffset(offset);
        sprite->SetUVMapping(uv);
        sprite->SetMode(state.mode);
    }

    for (const CameraState &state : m_Snapshot->cameras) {
        auto *camera = CKCamera::Cast(resolve(state.id));
        if (!camera)
            continue;
        camera->SetFrontPlane(state.frontPlane);
        camera->SetBackPlane(state.backPlane);
        camera->SetFov(state.fov);
        camera->SetProjectionType(state.projection);
        camera->SetOrthographicZoom(state.orthographicZoom);
        // Aspect ratio belongs to the live render target. Rewinding it after a
        // window resize recreates the old distorted viewport and breaks mouse
        // coordinate mapping. Projection/FOV are timeline state; dimensions
        // are deliberately left at their current value.
        camera->SetTarget(CK3dEntity::Cast(
            state.target ? resolve(state.target) : nullptr));
    }
    for (const LightState &state : m_Snapshot->lights) {
        auto *light = CKLight::Cast(resolve(state.id));
        if (!light)
            continue;
        light->SetColor(state.color);
        light->SetConstantAttenuation(state.constantAttenuation);
        light->SetLinearAttenuation(state.linearAttenuation);
        light->SetQuadraticAttenuation(state.quadraticAttenuation);
        light->SetType(state.type);
        light->SetRange(state.range);
        light->SetHotSpot(state.hotSpot);
        light->SetFallOff(state.fallOff);
        light->SetFallOffShape(state.fallOffShape);
        light->SetLightPower(state.power);
        light->Active(state.active ? TRUE : FALSE);
        light->SetSpecularFlag(state.specular ? TRUE : FALSE);
        light->SetTarget(CK3dEntity::Cast(
            state.target ? resolve(state.target) : nullptr));
    }

    // 2D HUD elements have their own hierarchy and animation state. Keep
    // children fixed while restoring each saved rect so parent updates cannot
    // accumulate offsets over repeated restores.
    size_t changed2dParents = 0;
    std::vector<const Entity2dState *> ordered2dHierarchy;
    ordered2dHierarchy.reserve(m_Snapshot->entities2d.size());
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        auto *parent = CK2dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (!entity)
            continue;
        int currentSiblingIndex = -1;
        if (entity->GetParent()) {
            CK2dEntity *currentParent = entity->GetParent();
            for (int child = 0; child < currentParent->GetChildrenCount(); ++child) {
                if (currentParent->GetChild(child) == entity) {
                    currentSiblingIndex = child;
                    break;
                }
            }
        }
        if (entity->GetParent() != parent ||
            (parent && currentSiblingIndex != state.siblingIndex))
            ++changed2dParents;
        ordered2dHierarchy.push_back(&state);
#ifndef __APPLE__
        if (entity->GetParent())
            entity->SetParent(nullptr);
#endif
    }
#ifndef __APPLE__
    std::stable_sort(ordered2dHierarchy.begin(), ordered2dHierarchy.end(),
        [](const Entity2dState *left, const Entity2dState *right) {
            if (left->parent != right->parent)
                return left->parent < right->parent;
            return left->siblingIndex < right->siblingIndex;
        });
    for (const Entity2dState *state : ordered2dHierarchy) {
        auto *entity = CK2dEntity::Cast(resolve(state->id));
        auto *parent = CK2dEntity::Cast(
            state->parent ? resolve(state->parent) : nullptr);
        if (entity && parent)
            entity->SetParent(parent);
    }
#endif
    size_t final2dHierarchyMismatches = 0;
#ifndef __APPLE__
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        auto *parent = CK2dEntity::Cast(state.parent ? resolve(state.parent) : nullptr);
        if (!entity || entity->GetParent() != parent) {
            if (entity)
                ++final2dHierarchyMismatches;
            continue;
        }
        if (parent && (state.siblingIndex < 0 ||
                       state.siblingIndex >= parent->GetChildrenCount() ||
                       parent->GetChild(state.siblingIndex) != entity))
            ++final2dHierarchyMismatches;
    }
#endif
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        if (!entity)
            continue;
#ifndef __APPLE__
        entity->SetFlags(state.flags);
        entity->SetMaterial(CKMaterial::Cast(
            state.material ? resolve(state.material) : nullptr));
        if (state.homogeneous)
            entity->SetHomogeneousRect(state.rect, TRUE);
        else
            entity->SetRect(state.rect, TRUE);
        VxRect sourceRect = state.sourceRect;
        entity->SetSourceRect(sourceRect);
        entity->SetZOrder(state.zOrder);
#endif
        entity->Show(state.visible ? CKSHOW : CKHIDE);
        if (state.spriteSlot >= 0 && CKIsChildClassOf(entity->GetClassID(), CKCID_SPRITE))
            static_cast<CKSprite *>(entity)->SetCurrentSlot(state.spriteSlot);
    }

    for (const SpriteState &state : m_Snapshot->sprites) {
        auto *sprite = CKSprite::Cast(resolve(state.id));
        if (!sprite)
            continue;
        sprite->SetTransparentColor(state.transparentColor);
        sprite->SetTransparent(state.transparent ? TRUE : FALSE);
    }
    for (const SpriteTextState &state : m_Snapshot->spriteTexts) {
        auto *sprite = CKSpriteText::Cast(resolve(state.id));
        if (!sprite)
            continue;
        sprite->SetText(const_cast<char *>(state.text.c_str()));
        sprite->SetTextColor(state.textColor);
        sprite->SetBackgroundColor(state.backgroundColor);
        sprite->SetAlign(state.alignment);
    }

#ifdef __APPLE__
    // Restore graph/configuration and cursors without evaluating any animation.
    // Entity, mesh and morph poses belong to their exact world snapshots; a
    // SetStep here would overwrite them using mutable future controller data.
    if (m_Snapshot->commitAnimationRestore(m_Context, animationRestore.get()) != CK_OK) {
        m_BML->SendIngameMessage("Unable to commit saved animation runtime graph");
        return false;
    }
#else
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

#endif

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

        if (m_Snapshot->environment.captured) {
            const SceneEnvironmentState &environment = m_Snapshot->environment;
            scene->SetAmbientLight(environment.ambientLight);
            scene->SetFogMode(environment.fogMode);
            scene->SetFogStart(environment.fogStart);
            scene->SetFogEnd(environment.fogEnd);
            scene->SetFogDensity(environment.fogDensity);
            scene->SetFogColor(environment.fogColor);
            scene->SetBackgroundColor(environment.backgroundColor);
            scene->SetBackgroundTexture(CKTexture::Cast(
                environment.backgroundTexture
                    ? resolve(environment.backgroundTexture) : nullptr));
            if (CKRenderContext *render = m_Context->GetPlayerRenderContext()) {
                CKCamera *camera = CKCamera::Cast(environment.attachedCamera
                    ? resolve(environment.attachedCamera) : nullptr);
                if (camera)
                    render->AttachViewpointToCamera(camera);
                else
                    render->DetachViewpointFromCamera();
                render->SetClearBackground(
                    environment.clearBackground ? TRUE : FALSE);
                render->ForceCameraSettingsUpdate();
            }
        }
    }

    // Reconcile which IVP objects exist before restoring behavior IO. The
    // physics layer may need to execute Physicalize/Continuous Contact while
    // rebuilding objects; the scheduler pass below then puts every input and
    // output back at the exact saved activation phase.
    if (TT_PhysicsRestoreSnapshot(m_Context, m_Snapshot->physics) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore physics state");
        return false;
    }
    if (TT_ParticleRestoreSnapshot(m_Context, m_Snapshot->particles) != CK_OK ||
        TT_ExtraCommitRestore(m_Context, extraRestore.get()) != CK_OK ||
        TT_SimpleShadowRestoreSnapshot(
            m_Context, m_Snapshot->simpleShadows) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore runtime state");
        return false;
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

        messages->ClearRuntimePendingMessages();
        messages->ClearRuntimeLastFrameMessages();
        const auto restoreMessage = [this, &resolve](
            const RuntimeMessageState &state) -> CKMessage * {
            CKMessage *message = new CKMessage(m_Context);
            if (!message)
                return nullptr;
            message->SetMsgType(state.type);
            message->SetSendingType(state.sendingType);
            message->SetSender(CKBeObject::Cast(
                state.sender ? resolve(state.sender) : nullptr));
            if (state.sendingType == CK_MESSAGE_BROADCAST)
                message->SetBroadcastObjectType(state.broadcastClass);
            else
                message->SetRecipient(
                    state.recipient ? resolve(state.recipient) : nullptr);
            for (const RuntimeMessageState::ParameterState &saved :
                 state.parameters) {
                if (!saved.value)
                    continue;
                CKParameterOut *copy = m_Context->CreateCKParameterOut(
                    nullptr, saved.value->GetGUID(), TRUE);
                if (!copy || copy->CopyValue(saved.value, FALSE) != CK_OK ||
                    PracticeTimeline::RestoreParameterReferences(copy, saved.references, resolve) != CK_OK ||
                    message->AddParameter(copy, TRUE) != CK_OK) {
                    if (copy)
                        m_Context->DestroyObject(copy);
                }
            }
            return message;
        };
        for (const RuntimeMessageState &state : m_Snapshot->pendingMessages) {
            CKMessage *message = restoreMessage(state);
            if (!message)
                continue;
            messages->SendMessage(message);
            message->Release();
        }
        for (const RuntimeMessageState &state : m_Snapshot->lastFrameMessages) {
            CKMessage *message = restoreMessage(state);
            CKBeObject *object = CKBeObject::Cast(
                state.lastFrameObject ? resolve(state.lastFrameObject) : nullptr);
            if (message && object)
                messages->AddRuntimeLastFrameMessage(object, message);
            if (message)
                message->Release();
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
    const CKERROR schedulerResult = scheduler->RestoreRuntimeState(m_Snapshot->scheduler);
    if (schedulerResult != CK_OK) {
        GetLogger()->Warn("Could not restore behavior scheduler state (%d).", schedulerResult);
        m_BML->SendIngameMessage("Unable to restore behavior scheduler state");
        return false;
    }

    const BML::Speedrun::TimerSnapshot timer{
        m_Snapshot->timerElapsed, m_Snapshot->timerRunning ? 1 : 0};
    if (!m_Snapshot->timerCaptured || BML::Speedrun::WriteTimerSnapshot(timer) != BML_OK) {
        m_BML->SendIngameMessage("Unable to restore precise SR timer state");
        return false;
    }
    if (m_Snapshot->clockCaptured)
        m_Time->RestoreRuntimeClock(m_Snapshot->absoluteTime,
                                    m_Snapshot->mainTick,
                                    m_Snapshot->lastDeltaTime,
                                    m_Snapshot->lastDeltaTimeFree);
    CKSetRandomState(m_Snapshot->randomState);
#ifdef __APPLE__
    if (m_Snapshot->commitSound(m_Context, soundRestore.get()) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore sound runtime state");
        return false;
    }
    GetLogger()->Info("Sound runtime restore committed.");
#endif
#if defined(__APPLE__)
    if (m_Snapshot->toolboxTimeCaptured)
        TT_TimeRestoreRuntimeStateV2(m_Context, m_Snapshot->toolboxFixedTicks,
                                   m_Snapshot->toolboxTime,
                                   m_Snapshot->toolboxTiming, m_Snapshot->toolboxRecordedClock);
#elif defined(__linux__)
    if (m_Snapshot->toolboxTimeCaptured) {
        if (RestoreToolboxTime restore = ResolveRestoreToolboxTime())
            restore(m_Context, m_Snapshot->toolboxFixedTicks,
                    m_Snapshot->toolboxTime, m_Snapshot->toolboxTiming,
                    m_Snapshot->toolboxRecordedClock);
    }
#endif
#ifdef __APPLE__
    // All hierarchy, mesh, animation and runtime setters have completed. Transfer
    // every stored matrix directly, so parent updates cannot perturb children.
    const auto restoreTransform = reinterpret_cast<RestoreEntityTransform>(
        ResolveEntityTransformSymbol("CKRestoreEntityTransformState"));
    const auto restoreUserBox = reinterpret_cast<RestoreEntityUserBox>(
        ResolveEntityTransformSymbol("CKRestoreEntityUserBoxState"));
    const auto restoreVisibility = reinterpret_cast<RestoreEntityVisibility>(
        ResolveEntityTransformSymbol("CKRestoreEntityVisibilityState"));
    size_t exactTransformsRestored = 0;
    for (const EntityState &state : m_Snapshot->entities) {
        if (!state.exactTransformCaptured) continue;
        auto *entity = CK3dEntity::Cast(resolve(state.id));
        if (!entity || !restoreTransform || !restoreUserBox || !restoreVisibility ||
            !restoreTransform(entity, &state.exactTransform, sizeof(state.exactTransform)) ||
            !restoreUserBox(entity, &state.userBox, sizeof(state.userBox)) ||
            !restoreVisibility(entity, &state.visibilityHistory, sizeof(state.visibilityHistory))) {
            m_BML->SendIngameMessage("Unable to restore exact entity transform");
            return false;
        }
        ++exactTransformsRestored;
    }
    if (exactTransformsRestored)
        GetLogger()->Info("Exact transform restore: %zu entities.", exactTransformsRestored);
#endif
#ifdef __APPLE__
    // Scene and semantic restoration can attach/detach drawing functions.
    // Restore their saved order only after their owners and transforms settle.
    if (m_Snapshot->restoreRenderCallbacks(m_Context, m_Snapshot->renderCallbacks) != CK_OK) {
        m_BML->SendIngameMessage("Unable to restore renderer callbacks");
        return false;
    }
    GetLogger()->Info("Renderer callback restore committed.");
#endif
    // The Virtools menu/scheduler and SR timer have already been restored.
#ifdef __APPLE__
    if (m_Snapshot->segmentLibrary) {
        if (!m_Snapshot->restoreSegment(m_Context, &m_Snapshot->segment)) {
            m_BML->SendIngameMessage("Unable to restore legacy split timer state");
            return false;
        }
        GetLogger()->Info("Segment restore: sector=%d elapsed=%a counting=%d duty=%lld.",
            m_Snapshot->segment.sector, m_Snapshot->segment.elapsed,
            int(m_Snapshot->segment.counting), m_Snapshot->segment.dutyCursor);
    }
#endif
    // Reconcile the loader's own phase without replaying Pause/Unpause events.
    if (BML::Gameplay::RestorePauseState(m_Snapshot->paused) != BML_OK) {
        m_BML->SendIngameMessage("Unable to restore loader pause state");
        return false;
    }
    GetLogger()->Info("Restore pause audit: before=%d final=%d expected=%d.",
                      int(currentPaused), int(m_BML->IsPaused()), int(m_Snapshot->paused));
#ifdef __APPLE__
    // Text, animation, plugin and pause setters have finished. Restore both
    // stored coordinate systems and the exact child order, including roots.
    const auto restore2d = reinterpret_cast<RestoreEntities2d>(
        ResolveEntityTransformSymbol("CKRestoreEntity2dStates"));
    std::vector<CKEntity2dRestoreEntry> layouts;
    layouts.reserve(m_Snapshot->entities2d.size());
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        if (!resolve(state.id)) {
            m_BML->SendIngameMessage("Saved 2D entity is unavailable");
            return false;
        }
        layouts.push_back({&state.exactLayout, state.children.data(), state.children.size()});
    }
    if (!restore2d || !restore2d(m_Context, layouts.data(), layouts.size(), sizeof(layouts[0]))) {
        m_BML->SendIngameMessage("Unable to restore exact 2D layout and hierarchy");
        return false;
    }
    for (const Entity2dState &state : m_Snapshot->entities2d) {
        auto *entity = CK2dEntity::Cast(resolve(state.id));
        const auto parent = entity->GetParent();
        if ((parent ? parent->GetID() : 0) != state.parent ||
            entity->GetChildrenCount() != static_cast<int>(state.children.size())) {
            ++final2dHierarchyMismatches;
            continue;
        }
        for (std::size_t child = 0; child < state.children.size(); ++child)
            if (entity->GetChild(static_cast<int>(child))->GetID() != state.children[child])
                ++final2dHierarchyMismatches;
    }
    if (final2dHierarchyMismatches) {
        m_BML->SendIngameMessage("Restored 2D hierarchy differs from snapshot");
        return false;
    }
    GetLogger()->Info("Exact 2D restore: %zu entities, ordered hierarchy verified.", layouts.size());
#endif
    AuditTimelineReferences(m_Context, GetLogger(), newTimelineIdentities, "after");
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
        "Restore topology audit: %zu/%zu hierarchy changes, %zu/%zu final hierarchy mismatches, %zu scene additions, "
        "%zu scene removals, %zu final scene mismatches, %zu stale referenced identities, "
        "%zu missing/%zu new ordinary timeline objects; "
        "%zu active/%zu pending behavior objects.",
        changed3dParents, changed2dParents,
        final3dHierarchyMismatches, final2dHierarchyMismatches, addedSceneObjects,
        removedSceneObjects, sceneMembershipMismatches, staleObjectIds.size(),
        missingSnapshotObjects, newTimelineObjects,
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
    return true;
}

bool PracticeTools::RestoreForAutomation() {
    if (!m_Context || !(m_BML->IsPlaying() || m_BML->IsPaused()) || !m_Snapshot)
        return false;
    return RestoreState();
}

void PracticeTools::ClearState() {
    DestroySnapshot(m_Snapshot.get());
    m_Snapshot.reset();
    DestroySnapshot(m_ReusableSnapshot.get());
    m_ReusableSnapshot.reset();
}

void PracticeTools::CollectSnapshotStorage(
    const Snapshot *snapshot, std::unordered_set<CKObject *> &storage) const {
    if (!snapshot) return;
    for (const ObjectState &state : snapshot->parameters)
        if (state.value) CollectSnapshotParameterStorage(m_Context, state.value, storage);
    for (const ArrayState &array : snapshot->arrays)
        for (const ArrayCellState &cell : array.cells)
            if (cell.parameter) CollectSnapshotParameterStorage(m_Context, cell.parameter, storage);
    const auto messages = [this, &storage](const std::vector<RuntimeMessageState> &entries) {
        for (const RuntimeMessageState &message : entries)
            for (const auto &parameter : message.parameters)
                if (parameter.value) CollectSnapshotParameterStorage(m_Context, parameter.value, storage);
    };
    messages(snapshot->pendingMessages);
    messages(snapshot->lastFrameMessages);
}

void PracticeTools::DestroySnapshot(Snapshot *snapshot) {
    if (!snapshot)
        return;
#ifdef __APPLE__
    if (snapshot->sound && snapshot->destroySound) snapshot->destroySound(snapshot->sound);
    snapshot->sound = nullptr;
    if (snapshot->soundLibrary) dlclose(snapshot->soundLibrary);
    snapshot->soundLibrary = nullptr;
    if (snapshot->segmentLibrary) dlclose(snapshot->segmentLibrary);
    snapshot->segmentLibrary = nullptr;
    snapshot->validateSegment = nullptr;
    snapshot->restoreSegment = nullptr;
    if (snapshot->animationState && snapshot->destroyAnimationState)
        snapshot->destroyAnimationState(snapshot->animationState);
    snapshot->animationState = nullptr;
    if (snapshot->renderCallbacks && snapshot->destroyRenderCallbacks)
        snapshot->destroyRenderCallbacks(snapshot->renderCallbacks);
    snapshot->renderCallbacks = nullptr;
#endif
    CKBehaviorManager::ReleaseRuntimeState(snapshot->scheduler);
    snapshot->scheduler = nullptr;
    if (snapshot->physics) {
        TT_PhysicsDestroySnapshot(snapshot->physics);
        snapshot->physics = nullptr;
    }
    if (snapshot->particles) {
        TT_ParticleDestroySnapshot(snapshot->particles);
        snapshot->particles = nullptr;
    }
    if (snapshot->extras) {
        TT_ExtraDestroySnapshot(snapshot->extras);
        snapshot->extras = nullptr;
    }
    if (snapshot->simpleShadows) {
        TT_SimpleShadowDestroySnapshot(snapshot->simpleShadows);
        snapshot->simpleShadows = nullptr;
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
        const auto destroyMessageParameters = [this](
            std::vector<RuntimeMessageState> &messages) {
            for (RuntimeMessageState &message : messages) {
                for (RuntimeMessageState::ParameterState &parameter :
                     message.parameters) {
                    if (parameter.value && !parameter.value->IsToBeDeleted())
                        m_Context->DestroyObject(parameter.value);
                    parameter.value = nullptr;
                }
            }
        };
        destroyMessageParameters(snapshot->pendingMessages);
        destroyMessageParameters(snapshot->lastFrameMessages);
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
    if (TasOwnsKeyboard(m_Input)) {
        GetLogger()->Info("Movement input retained from the active TAS stream.");
        return;
    }
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
