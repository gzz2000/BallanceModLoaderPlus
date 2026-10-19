#pragma once

#include <memory>
#include <string>
#include <vector>

#include <BML/BMLAll.h>

MOD_EXPORT IMod *BMLEntry(IBML *bml);
MOD_EXPORT void BMLExit(IMod *mod);

using TT_PhysicsSnapshotHandle = void *;
extern "C" int TT_PhysicsCaptureSnapshot(CKContext *, TT_PhysicsSnapshotHandle *);
extern "C" int TT_PhysicsRestoreSnapshot(CKContext *, TT_PhysicsSnapshotHandle);
extern "C" void TT_PhysicsDestroySnapshot(TT_PhysicsSnapshotHandle);

class PracticeTools final : public IMod {
public:
    explicit PracticeTools(IBML *bml) : IMod(bml) {}
    ~PracticeTools() override;

    const char *GetID() override { return "PracticeTools"; }
    const char *GetVersion() override { return "0.1.4"; }
    const char *GetName() override { return "Practice Tools"; }
    const char *GetAuthor() override { return "gzz2000"; }
    const char *GetDescription() override { return "Hold-to-run-at-264 and in-memory gameplay snapshots."; }
    DECLARE_BML_VERSION;

    void OnLoad() override;
    void OnUnload() override;
    void OnModifyConfig(const char *, const char *, IProperty *) override;
    void OnProcess() override;
    void OnPreLoadLevel() override;
    void OnPreExitLevel() override;
    void OnPreNextLevel() override;

private:
    struct ObjectState {
        CK_ID id = 0;
        CKParameterOut *value = nullptr;
    };

    struct ArrayCellState {
        int row = 0;
        int column = 0;
        CK_ARRAYTYPE type = CKARRAYTYPE_INT;
        CKDWORD scalar = 0;
        std::string string;
        CKParameterOut *parameter = nullptr;
    };

    struct ArrayState {
        CK_ID id = 0;
        int rows = 0;
        int columns = 0;
        std::vector<ArrayCellState> cells;
    };

    struct EntityState {
        CK_ID id = 0;
        VxMatrix local;
        bool visible = false;
    };

    struct Entity2dState {
        CK_ID id = 0;
        VxRect rect;
        VxRect sourceRect;
        CKDWORD flags = 0;
        int zOrder = 0;
        int spriteSlot = -1;
        bool homogeneous = false;
        bool visible = false;
    };

    struct BehaviorState {
        CK_ID id = 0;
        CKDWORD runtimeFlags = 0;
        std::vector<CK_ID> delayedLinks;
    };

    struct BehaviorIoState {
        CK_ID id = 0;
        CKDWORD flags = 0;
    };

    struct BehaviorLinkState {
        CK_ID id = 0;
        int activationDelay = 0;
        CKDWORD runtimeFlags = 0;
    };

    struct ObjectAnimationState {
        CK_ID id = 0;
        float step = 0.0f;
    };

    struct AnimationState {
        CK_ID id = 0;
        float step = 0.0f;
    };

    struct StateObjectState {
        CK_ID id = 0;
        bool active = false;
    };

    struct SynchroState {
        CK_ID id = 0;
        int waiters = 0;
        std::vector<CK_ID> arrived;
        std::vector<CK_ID> passed;
    };

    struct CriticalSectionState {
        CK_ID id = 0;
        CK_ID owner = 0;
    };

    struct MessageWaitState {
        CKMessageType type = -1;
        CK_ID behavior = 0;
        CK_ID output = 0;
        CK_ID object = 0;
    };

    struct SceneObjectState {
        CK_ID id = 0;
        bool active = false;
    };

    struct Snapshot {
        std::vector<CK_ID> objectIds;
        std::vector<ObjectState> parameters;
        std::vector<ArrayState> arrays;
        std::vector<EntityState> entities;
        std::vector<Entity2dState> entities2d;
        std::vector<BehaviorState> behaviors;
        std::vector<BehaviorIoState> behaviorIos;
        std::vector<BehaviorLinkState> behaviorLinks;
        std::vector<ObjectAnimationState> objectAnimations;
        std::vector<AnimationState> animations;
        std::vector<SceneObjectState> sceneObjects;
        std::vector<StateObjectState> stateObjects;
        std::vector<SynchroState> synchros;
        std::vector<CriticalSectionState> criticalSections;
        std::vector<MessageWaitState> messageWaits;
        TT_PhysicsSnapshotHandle physics = nullptr;
        float timerElapsed = 0.0f;
        bool timerRunning = false;
        bool timerCaptured = false;
        int energyPoints = 0;
        int levelPoints = 0;
        bool scoreCaptured = false;
        float absoluteTime = 0.0f;
        CKDWORD mainTick = 0;
        bool clockCaptured = false;
    };

    void RefreshSettings();
    void BeginFullSpeed();
    void EndFullSpeed();
    void SaveState();
    void RestoreState();
    void ClearState();

    CKContext *m_Context = nullptr;
    CKTimeManager *m_Time = nullptr;
    CKInputManager *m_Input = nullptr;
    IProperty *m_FullSpeedProperty = nullptr;
    IProperty *m_SaveProperty = nullptr;
    IProperty *m_RestoreProperty = nullptr;
    CKKEYBOARD m_FullSpeedKey = CKKEY_TAB;
    CKKEYBOARD m_SaveKey = CKKEY_F5;
    CKKEYBOARD m_RestoreKey = CKKEY_F8;

    bool m_FullSpeedActive = false;
    bool m_SaveDown = false;
    bool m_RestoreDown = false;
    CK_FRAMERATE_LIMITS m_PreviousFrameMode = CK_FRAMERATE_SYNC;
    float m_PreviousFrameLimit = 60.0f;
    float m_PreviousProcessRate = 264.0f;
    std::unique_ptr<Snapshot> m_Snapshot;
};
