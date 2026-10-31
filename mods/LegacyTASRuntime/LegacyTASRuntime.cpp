// Native compatibility adapter. See component PROVENANCE.md and LICENSE files.
#include <BML/IMod.h>
#include "Segment.h"
#include "Layout.h"
#include "HelperRuntime.h"
#include "GuiRuntime.h"
#include <memory>
#include <BML/BML.h>
#include <BML/ICommand.h>
#include <BML/ScriptHelper.h>
#include <cstdlib>
#include "InterfaceManager.h"
#include <stdexcept>

class LegacyTASRuntime;
static LegacyTASRuntime *checkpointProvider = nullptr;

class LegacyTASRuntime : public LegacySegment127::Segment, public LegacyTASGui::Host {
public:
    explicit LegacyTASRuntime(IBML *b):Segment(b){}
    const char *GetName() override { return "Legacy TAS Runtime"; }
    const char *GetDescription() override {
        return "Legacy Segment/environment compatibility with the native TAS project menu.";
    }
    IBML *GetBML() override { return m_BML; }
    LegacyBGui::Runtime &GetGuiRuntime() override { return *guiRuntime; }
    std::filesystem::path RecordDirectory() const override {
        const char *path = BML_GetLoaderPathUtf8(BML_DIR_LOADER);
        return path && *path ? std::filesystem::u8path(path) / "TAS" : std::filesystem::path{};
    }
    void LoadTAS(const std::string &path) override {
        auto *command = m_BML->FindCommand("tasplay");
        if (!command) { m_BML->SendIngameMessage("BallanceTAS playback is unavailable."); return; }
        command->Execute(m_BML, {"tasplay", path});
    }
    bool OpenProjectMenu() override {
        auto *command = m_BML->FindCommand("tasmenu");
        if (!command) return false;
        command->Execute(m_BML, {"tasmenu"});
        return true;
    }
    void LeaveStartMenu() override {
        auto *start = m_BML->GetScriptByName("Menu_Start");
        auto *exit = start ? ScriptHelper::FindFirstBB(start, "Exit") : nullptr;
        if (!exit) { m_BML->SendIngameMessage("The level selection menu is not available."); return; }
        exit->ActivateInput(0); exit->Activate();
    }
    void OnLoad() override {
        helpers=std::make_unique<LegacyExecuteBB::Runtime>(m_BML->GetCKContext());
        guiRuntime=std::make_unique<LegacyBGui::Runtime>(m_BML,*helpers);
        AttachGuiRuntime(*guiRuntime);
        helpers->LoadAuxiliaryAssets();
        Segment::OnLoad();
        layout=new LegacyTASGui::Layout(*this);
        layout->Initialize();
        checkpointProvider = this;
        GetLogger()->Info("LEGACY_RUNTIME_READY");
        GetLogger()->Info("LEGACY_RUNTIME_SERVICES segment_events=%d tas_menu=%d tas_keys=0",active,active);
    }
    void OnLoadObject(CKSTRING filename, CKBOOL isMap, CKSTRING masterName,
        CK_CLASSID filterClass, CKBOOL addtoscene, CKBOOL reuseMeshes,
        CKBOOL reuseMaterials, CKBOOL dynamic, XObjectArray *objects, CKObject *master) override {
        if (!active) return;
        std::string path=filename?filename:"";
        std::replace(path.begin(),path.end(),'\\','/');
        if (path=="3D Entities/Menu.nmo") {
            guiRuntime->InitMaterials();
            levelButton=guiRuntime->Keep(m_BML->Get2dEntityByName("M_Start_But_01"));
            GetLogger()->Info("LEGACY_GAMEPLAY_MENU materials=1 level_button=%d",levelButton.Get()!=nullptr);
        }
        Segment::OnLoadObject(filename,isMap,masterName,filterClass,addtoscene,reuseMeshes,reuseMaterials,dynamic,objects,master);
        if(isMap)GetLogger()->Info("LEGACY_GAMEPLAY_MAP %s",filename);
    }
    void OnProcess() override {
        if (!active) return;
        Segment::OnProcess();
        layout->ProcessMenu(levelButton.Get());
        ++frames;
    }
    void OnStartLevel() override {
        if (!active) return;
        Segment::OnStartLevel();GetLogger()->Info("LEGACY_GAMEPLAY_START");
    }
    void OnPreCheckpointReached() override {
        if (!active) return;
        Segment::OnPreCheckpointReached();GetLogger()->Info("LEGACY_GAMEPLAY_CHECKPOINT");
    }
    void OnPreEndLevel() override {
        if (!active) return;
        Segment::OnPreEndLevel();GetLogger()->Info("LEGACY_GAMEPLAY_FINISH");
    }
    LegacyBGui::ObjectRef<CK2dEntity> levelButton;
    void OnCounterActive() override { if(active)Segment::OnCounterActive(); }
    void OnCounterInactive() override { if(active)Segment::OnCounterInactive(); }
    void OnPauseLevel() override { if(active)Segment::OnPauseLevel(); }
    void OnUnpauseLevel() override { if(active)Segment::OnUnpauseLevel(); }
    void OnPreStartMenu() override { if(active)Segment::OnPreStartMenu(); }
    void OnPreExitLevel() override { if(active)Segment::OnPreExitLevel(); }
    void OnGameOver() override { if(active)Segment::OnGameOver(); }
    void OnCheatEnabled(bool enabled) override { if(active)Segment::OnCheatEnabled(enabled); }
    void OnModifyConfig(CKSTRING category, CKSTRING key, IProperty *prop) override {
        if(active)Segment::OnModifyConfig(category,key,prop);
    }
    bool active=true;
    unsigned frames=0;
    LegacyTASGui::Layout *layout=nullptr;
    void OnUnload() override {
        if (checkpointProvider == this) checkpointProvider = nullptr;
        levelButton={};
        delete layout;layout=nullptr;
        Segment::OnUnload();
        guiRuntime.reset();helpers.reset();
    }
    ~LegacyTASRuntime() override { OnUnload(); }
    std::unique_ptr<LegacyExecuteBB::Runtime> helpers;
    std::unique_ptr<LegacyBGui::Runtime> guiRuntime;
};
MOD_EXPORT IMod *BMLEntry(IBML *bml){return new LegacyTASRuntime(bml);}
MOD_EXPORT void BMLExit(IMod *mod){delete mod;}

MOD_EXPORT int BML_LegacySegmentCapture(CKContext *context, LegacySegmentCheckpointV1 *state) {
    return checkpointProvider && state && state->size == sizeof(*state) && state->version == 1 &&
        checkpointProvider->GetBML()->GetCKContext() == context &&
        checkpointProvider->CaptureCheckpoint(*state);
}
MOD_EXPORT int BML_LegacySegmentValidate(CKContext *context, const LegacySegmentCheckpointV1 *state) {
    return checkpointProvider && state && checkpointProvider->GetBML()->GetCKContext() == context &&
        checkpointProvider->ValidateCheckpoint(*state);
}
MOD_EXPORT int BML_LegacySegmentRestore(CKContext *context, const LegacySegmentCheckpointV1 *state) {
    return checkpointProvider && state && checkpointProvider->GetBML()->GetCKContext() == context &&
        checkpointProvider->RestoreCheckpoint(*state);
}
