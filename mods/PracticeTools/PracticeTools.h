#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <BML/BMLAll.h>
#include "OutputTopology.h"
#include "InputTopology.h"
#include "ParameterReferences.h"
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
#include <CKEntityTransformState.h>
#include <CKEntity2dState.h>
#include <CKEntityUserBoxState.h>
#include <CKEntityVisibilityState.h>
#include "../LegacyTASRuntime/SegmentCheckpoint.h"
#endif

MOD_EXPORT IMod *BMLEntry(IBML *bml);
MOD_EXPORT void BMLExit(IMod *mod);

using TT_PhysicsSnapshotHandle = void *;
using TT_ParticleSnapshotHandle = void *;
using TT_ExtraSnapshotHandle = void *;
using TT_ExtraRestoreHandle = void *;
using TT_SimpleShadowSnapshotHandle = void *;
extern "C" int TT_PhysicsCaptureSnapshot(CKContext *, TT_PhysicsSnapshotHandle *);
extern "C" int TT_PhysicsRestoreSnapshot(CKContext *, TT_PhysicsSnapshotHandle);
extern "C" void TT_PhysicsDestroySnapshot(TT_PhysicsSnapshotHandle);
extern "C" int TT_ParticleCaptureSnapshot(CKContext *, TT_ParticleSnapshotHandle *);
extern "C" int TT_ParticleRestoreSnapshot(CKContext *, TT_ParticleSnapshotHandle);
extern "C" int TT_ParticleValidateSnapshot(CKContext *, TT_ParticleSnapshotHandle);
extern "C" void TT_ParticleDestroySnapshot(TT_ParticleSnapshotHandle);
extern "C" int TT_ExtraCaptureSnapshot(CKContext *, TT_ExtraSnapshotHandle *);
extern "C" int TT_ExtraRestoreSnapshot(CKContext *, TT_ExtraSnapshotHandle);
extern "C" void TT_ExtraDestroySnapshot(TT_ExtraSnapshotHandle);
extern "C" int TT_ExtraPrepareRestore(CKContext *, TT_ExtraSnapshotHandle, TT_ExtraRestoreHandle *);
extern "C" int TT_ExtraCommitRestore(CKContext *, TT_ExtraRestoreHandle);
extern "C" void TT_ExtraDestroyRestore(TT_ExtraRestoreHandle);
extern "C" int TT_SimpleShadowCaptureSnapshot(
    CKContext *, TT_SimpleShadowSnapshotHandle *);
extern "C" int TT_SimpleShadowRestoreSnapshot(
    CKContext *, TT_SimpleShadowSnapshotHandle);
extern "C" void TT_SimpleShadowDestroySnapshot(
    TT_SimpleShadowSnapshotHandle);
extern "C" int TT_SimpleShadowIsRuntimeMaterial(CKContext *, CKMaterial *);
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
extern "C" int TT_TimeCaptureRuntimeStateV2(
    CKContext *, std::uint64_t *, double *, CKBOOL *, CKBOOL *);
extern "C" int TT_TimeRestoreRuntimeStateV2(
    CKContext *, std::uint64_t, double, CKBOOL, CKBOOL);
#endif

class PracticeTools final : public IMod {
public:
    explicit PracticeTools(IBML *bml) : IMod(bml) {}
    ~PracticeTools() override;

    const char *GetID() override { return "PracticeTools"; }
    const char *GetVersion() override { return "0.1.10"; }
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
    void OnBallNavActive() override;

    // Deterministic harness entry points. They run on the game thread and use
    // exactly the same implementation as the configured save/restore controls.
    bool CaptureForAutomation();
    bool RestoreForAutomation();

private:
    struct ObjectState {
        CK_ID id = 0;
        CKParameterOut *value = nullptr;
        PracticeTimeline::ParameterReferences references;
    };

    using ParameterInState = PracticeTimeline::InputState;

    struct BeObjectState {
        CK_ID id = 0;
        int priority = 0;
        int messageWaitingCount = 0;
        std::vector<CK_ID> scripts;
    };

    struct ArrayCellState {
        int row = 0;
        int column = 0;
        CK_ARRAYTYPE type = CKARRAYTYPE_INT;
        CKDWORD scalar = 0;
        std::string string;
        CK_ID source = 0;
        bool owned = false;
        CKParameterOut *parameter = nullptr;
        PracticeTimeline::ParameterReferences references;
    };

    struct ArrayState {
        struct ColumnState {
            CK_ARRAYTYPE type = CKARRAYTYPE_INT;
            CKGUID parameterGuid;
            std::string name;
        };
        CK_ID id = 0;
        int rows = 0;
        int columns = 0;
        int keyColumn = -1;
        std::vector<ColumnState> columnStates;
        std::vector<ArrayCellState> cells;
    };

    struct AttributeState {
        struct Entry {
            CKAttributeType type = -1;
            CK_ID parameter = 0;
        };
        CK_ID id = 0;
        std::vector<Entry> entries;
    };

    struct EntityState {
        CK_ID id = 0;
        CK_ID parent = 0;
        int siblingIndex = -1;
        CK_ID currentMesh = 0;
        std::vector<CK_ID> meshes;
        VxMatrix local;
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
        CKEntityTransformState exactTransform;
        CKEntityUserBoxState userBox;
        CKEntityVisibilityState visibilityHistory;
        bool exactTransformCaptured = false;
#endif
        CKDWORD flags = 0;
        CKDWORD moveableFlags = 0;
        int zOrder = 0;
        bool visible = false;
    };

    struct TextureState {
        CK_ID id = 0;
        int currentSlot = 0;
        CKDWORD transparentColor = 0;
        bool transparent = false;
    };

    struct Sprite3dState {
        CK_ID id = 0;
        CK_ID material = 0;
        Vx2DVector size;
        Vx2DVector offset;
        VxRect uv;
        VXSPRITE3D_TYPE mode = VXSPRITE3D_BILLBOARD;
    };

    struct Entity2dState {
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
        CKEntity2dState exactLayout;
        std::vector<CK_ID> children;
#endif
        CK_ID id = 0;
        CK_ID parent = 0;
        int siblingIndex = -1;
        VxRect rect;
        VxRect sourceRect;
        CKDWORD flags = 0;
        int zOrder = 0;
        CK_ID material = 0;
        int spriteSlot = -1;
        bool homogeneous = false;
        bool visible = false;
    };

    struct SpriteState {
        CK_ID id = 0;
        CKDWORD transparentColor = 0;
        bool transparent = false;
    };

    struct SpriteTextState {
        CK_ID id = 0;
        std::string text;
        CKDWORD textColor = 0;
        CKDWORD backgroundColor = 0;
        CKSPRITETEXT_ALIGNMENT alignment = CKSPRITETEXT_LEFT;
    };

    struct CameraState {
        CK_ID id = 0;
        float frontPlane = 0.0f;
        float backPlane = 0.0f;
        float fov = 0.0f;
        int projection = 0;
        float orthographicZoom = 0.0f;
        int aspectWidth = 0;
        int aspectHeight = 0;
        CK_ID target = 0;
    };

    struct LightState {
        CK_ID id = 0;
        VxColor color;
        float constantAttenuation = 0.0f;
        float linearAttenuation = 0.0f;
        float quadraticAttenuation = 0.0f;
        VXLIGHT_TYPE type = VX_LIGHTPOINT;
        float range = 0.0f;
        float hotSpot = 0.0f;
        float fallOff = 0.0f;
        float fallOffShape = 0.0f;
        float power = 0.0f;
        bool active = false;
        bool specular = false;
        CK_ID target = 0;
    };

    struct MaterialState {
        CK_ID id = 0;
        float power = 0.0f;
        VxColor ambient;
        VxColor diffuse;
        VxColor specular;
        VxColor emissive;
        CK_ID textures[4]{};
        VXTEXTURE_BLENDMODE textureBlend = VXTEXTUREBLEND_DECAL;
        VXTEXTURE_FILTERMODE textureMin = VXTEXTUREFILTER_NEAREST;
        VXTEXTURE_FILTERMODE textureMag = VXTEXTUREFILTER_NEAREST;
        VXTEXTURE_ADDRESSMODE textureAddress = VXTEXTURE_ADDRESSWRAP;
        CKDWORD textureBorderColor = 0;
        VXBLEND_MODE sourceBlend = VXBLEND_ONE;
        VXBLEND_MODE destinationBlend = VXBLEND_ZERO;
        VXCMPFUNC zFunction = VXCMP_LESSEQUAL;
        VXFILL_MODE fillMode = VXFILL_SOLID;
        VXSHADE_MODE shadeMode = VXSHADE_GOURAUD;
        VXCMPFUNC alphaFunction = VXCMP_ALWAYS;
        CKBYTE alphaReference = 0;
        VX_EFFECT effect = VXEFFECT_NONE;
        // SetEffect can destroy/retype this owner-created parameter and its
        // structure children. Their saved IDs denote material slots, not
        // ordinary identities that must survive the abandoned timeline.
        PracticeTimeline::ParameterReferences effectParameter;
        bool twoSided = false;
        bool zWrite = false;
        bool alphaBlend = false;
        bool perspectiveCorrection = false;
        bool alphaTest = false;
    };

    struct MeshChannelState {
        CK_ID material = 0;
        CKDWORD flags = 0;
        VXBLEND_MODE sourceBlend = VXBLEND_ONE;
        VXBLEND_MODE destinationBlend = VXBLEND_ZERO;
        std::vector<Vx2DVector> uvs;
    };

    struct MeshFaceState {
        int vertices[3]{};
        CK_ID material = 0;
        CKWORD channelMask = 0;
    };

    struct MeshState {
        CK_ID id = 0;
        CKDWORD flags = 0;
        VXTEXTURE_WRAPMODE wrapMode = VXTEXTUREWRAP_NONE;
        VXMESH_LITMODE litMode = VX_PRELITMESH;
        bool transparent = false;
        std::vector<VxVector> positions;
        std::vector<VxVector> normals;
        std::vector<CKDWORD> colors;
        std::vector<CKDWORD> specularColors;
        std::vector<MeshChannelState> channels;
        std::vector<MeshFaceState> faces;
        std::vector<CKWORD> lines;
    };

    struct SceneEnvironmentState {
        CKDWORD ambientLight = 0;
        VXFOG_MODE fogMode = VXFOG_NONE;
        float fogStart = 0.0f;
        float fogEnd = 0.0f;
        float fogDensity = 0.0f;
        CKDWORD fogColor = 0;
        CKDWORD backgroundColor = 0;
        CK_ID backgroundTexture = 0;
        CK_ID attachedCamera = 0;
        bool clearBackground = false;
        bool captured = false;
    };

    struct BehaviorState {
        CK_ID id = 0;
        CKDWORD runtimeFlags = 0;
        std::vector<CK_ID> delayedLinks;
    };

    struct BehaviorIoState {
        CK_ID id = 0;
        CKDWORD flags = 0;
        bool active = false;
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

    struct RuntimeMessageState {
        struct ParameterState {
            CKParameterOut *value = nullptr;
            PracticeTimeline::ParameterReferences references;
        };
        CKMessageType type = -1;
        CK_MESSAGE_SENDINGTYPE sendingType = CK_MESSAGE_SINGLE;
        CK_ID sender = 0;
        CK_ID recipient = 0;
        CK_CLASSID broadcastClass = CKCID_BEOBJECT;
        CK_ID lastFrameObject = 0;
        std::vector<ParameterState> parameters;
    };

    struct SceneObjectState {
        CK_ID id = 0;
        CKDWORD flags = 0;
        bool inScene = false;
    };

    struct GroupState {
        CK_ID id = 0;
        std::vector<CK_ID> members;
    };

    struct Snapshot {
        struct IdentityInfo {
            CK_CLASSID classId = CKCID_OBJECT;
            std::string name;
        };
        // CK_ID slots can be reused after scripts delete transient objects.
        // Address equality alone is insufficient: allocators can reuse it too.
        // Native tokens describe registrations and never rewind with gameplay.
        std::unordered_map<CK_ID, CKObject *> identities;
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
        std::unordered_map<CK_ID, uint64_t> lifetimeTokens;
#endif
        std::unordered_map<CK_ID, IdentityInfo> identityInfo;
        std::unordered_set<CK_ID> arrayCellIdentities;
        std::unordered_set<CK_ID> attributeParameterIdentities;
        std::unordered_set<CK_ID> effectParameterIdentities;
        std::vector<ObjectState> parameters;
        std::vector<ParameterInState> parameterInputs;
        std::vector<PracticeTimeline::OutputState> parameterOutputs;
        std::vector<BeObjectState> beObjects;
        std::vector<ArrayState> arrays;
        std::vector<AttributeState> attributes;
        std::vector<EntityState> entities;
        std::vector<Entity2dState> entities2d;
        std::vector<SpriteState> sprites;
        std::vector<SpriteTextState> spriteTexts;
        std::vector<TextureState> textures;
        std::vector<Sprite3dState> sprites3d;
        std::vector<CameraState> cameras;
        std::vector<LightState> lights;
        std::vector<MaterialState> materials;
        std::vector<MeshState> meshes;
        SceneEnvironmentState environment;
        std::vector<BehaviorState> behaviors;
        std::vector<BehaviorIoState> behaviorIos;
        std::vector<BehaviorLinkState> behaviorLinks;
        std::vector<ObjectAnimationState> objectAnimations;
        std::vector<AnimationState> animations;
        std::vector<SceneObjectState> sceneObjects;
        std::vector<GroupState> groups;
        std::vector<StateObjectState> stateObjects;
        std::vector<SynchroState> synchros;
        std::vector<CriticalSectionState> criticalSections;
        std::vector<MessageWaitState> messageWaits;
        std::vector<RuntimeMessageState> pendingMessages;
        std::vector<RuntimeMessageState> lastFrameMessages;
        std::vector<CK_ID> activeBehaviorObjects;
        std::vector<CK_ID> pendingBehaviorObjects;
        std::vector<int> pendingBehaviorOperations;
        CKBehaviorRuntimeState *scheduler = nullptr;
        TT_PhysicsSnapshotHandle physics = nullptr;
        TT_ParticleSnapshotHandle particles = nullptr;
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
        void *soundLibrary = nullptr;
        void *sound = nullptr;
        int (*prepareSound)(CKContext *, void *, void **) = nullptr;
        int (*commitSound)(CKContext *, void *) = nullptr;
        void (*destroySoundRestore)(void *) = nullptr;
        void (*destroySound)(void *) = nullptr;
        void *animationState = nullptr;
        int (*prepareAnimationRestore)(CKContext *, void *, void **) = nullptr;
        int (*commitAnimationRestore)(CKContext *, void *) = nullptr;
        void (*destroyAnimationRestore)(void *) = nullptr;
        void (*destroyAnimationState)(void *) = nullptr;
        void *renderCallbacks = nullptr;
        int (*validateRenderCallbacks)(CKContext *, void *) = nullptr;
        int (*restoreRenderCallbacks)(CKContext *, void *) = nullptr;
        void (*destroyRenderCallbacks)(void *) = nullptr;
        void *segmentLibrary = nullptr;
        LegacySegmentCheckpointV1 segment;
        int (*validateSegment)(CKContext *, const LegacySegmentCheckpointV1 *) = nullptr;
        int (*restoreSegment)(CKContext *, const LegacySegmentCheckpointV1 *) = nullptr;
#endif
        TT_ExtraSnapshotHandle extras = nullptr;
        TT_SimpleShadowSnapshotHandle simpleShadows = nullptr;
        double timerElapsed = 0.0;
        bool timerRunning = false;
        bool timerCaptured = false;
        bool paused = false;
        int energyPoints = 0;
        int levelPoints = 0;
        bool scoreCaptured = false;
        float absoluteTime = 0.0f;
        float lastDeltaTime = 0.0f;
        float lastDeltaTimeFree = 0.0f;
        CKDWORD mainTick = 0;
        bool clockCaptured = false;
        std::uint64_t toolboxFixedTicks = 0;
        double toolboxTime = 0.0;
        CKBOOL toolboxTiming = FALSE;
        CKBOOL toolboxRecordedClock = FALSE;
        bool toolboxTimeCaptured = false;
        unsigned int randomState = 1;
    };

    void RefreshSettings();
    void BeginFullSpeed();
    void EndFullSpeed();
    bool SaveState();
    bool RestoreState();
    void ClearState();
    void DestroySnapshot(Snapshot *snapshot);
    void CollectSnapshotStorage(const Snapshot *snapshot,
        std::unordered_set<CKObject *> &storage) const;
    void SynchronizeMovementInput();

    CKContext *m_Context = nullptr;
    CKTimeManager *m_Time = nullptr;
    CKInputManager *m_Input = nullptr;
    IProperty *m_FullSpeedProperty = nullptr;
    IProperty *m_SaveProperty = nullptr;
    IProperty *m_RestoreProperty = nullptr;
    CKKEYBOARD m_FullSpeedKey = CKKEY_TAB;
    CKKEYBOARD m_SaveKey = CKKEY_A;
    CKKEYBOARD m_RestoreKey = CKKEY_Z;

    bool m_FullSpeedActive = false;
    bool m_SaveDown = false;
    bool m_RestoreDown = false;
    CK_FRAMERATE_LIMITS m_PreviousFrameMode = CK_FRAMERATE_SYNC;
    float m_PreviousFrameLimit = 60.0f;
    float m_PreviousProcessRate = 264.0f;
    std::unique_ptr<Snapshot> m_Snapshot;
    // Uncommitted value clones only; excluded from world capture and restoration.
    std::unique_ptr<Snapshot> m_ReusableSnapshot;
};
