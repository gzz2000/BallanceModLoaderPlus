#include <BML/IMod.h>
#include "Segment.h"
#include "Layout.h"
#include "HelperRuntime.h"
#include "GuiRuntime.h"
#include <memory>
#include <set>
#include <cstdlib>
#include "InterfaceManager.h"
#include <stdexcept>
#include "GraphDump.h"
class LegacySegmentProbe : public LegacySegment127::Segment, public LegacyTASGui::Host {
public:
    explicit LegacySegmentProbe(IBML *b):Segment(b){}
    void Inventory(const char *phase) {
        auto *ctx=m_BML->GetCKContext();
        GetLogger()->Info("LEGACY_HELPERS_BEGIN phase=%s count=%d",phase,ctx->GetObjectCount());
        for(int id=1;id<=ctx->GetObjectCount();++id) {
            auto *o=ctx->GetObject(id);
            GetLogger()->Info("LEGACY_HELPERS_ITEM phase=%s id=%d present=%d class=%d flags=%08x name=%s",phase,id,o!=nullptr,o?o->GetClassID():0,o?unsigned(o->GetObjectFlags()):0,o&&o->GetName()?o->GetName():"<null>");
        }
    }
    IBML *GetBML() override { return m_BML; }
    LegacyBGui::Runtime &GetGuiRuntime() override { return *guiRuntime; }
    std::filesystem::path RecordDirectory() const override { return "unused-construction-probe"; }
    void LoadTAS(const std::string &) override { throw std::logic_error("Playback is not exercised by this construction probe"); }
    void LeaveStartMenu() override { throw std::logic_error("Menu interaction is not exercised by this construction probe"); }
    void OnLoad() override {
        helpers=std::make_unique<LegacyExecuteBB::Runtime>(m_BML->GetCKContext());
        guiRuntime=std::make_unique<LegacyBGui::Runtime>(m_BML,*helpers);
        AttachGuiRuntime(*guiRuntime);
        Inventory("before");
        int base=m_BML->GetCKContext()->GetObjectCount();
        helpers->LoadAuxiliaryAssets();
        Inventory("assets");
        Segment::OnLoad();
        Inventory("segment");
        layout=new LegacyTASGui::Layout(*this);
        layout->Initialize();
        Inventory("after");
        DumpLegacyGraph(GetLogger(),m_BML->GetCKContext(),base);
        GetLogger()->Info("LEGACY_ASSETSGUI_COMPLETE");
        TestLifetime();
    }
    static void Require(bool ok,const char *message) {
        if (!ok) { std::fprintf(stderr,"GUI_LIFETIME_FAIL %s\n",message);std::fflush(stderr);std::_Exit(42); }
    }
    std::set<CK_ID> LiveIds() {
        std::set<CK_ID> ids;auto *ctx=m_BML->GetCKContext();
        for (int id=1;id<=ctx->GetObjectCount();++id) if(ctx->GetObject(id)) ids.insert(id);
        return ids;
    }
    void TestLifetime() {
        auto *ctx=m_BML->GetCKContext();
        auto *owner=static_cast<CKBehavior *>(ctx->GetObjectByNameAndClass("Level_Init",CKCID_BEHAVIOR));
        const auto before=LiveIds();const int parameters=owner->GetLocalParameterCount();
        int clicks=0;
        {
            LegacyBGui::Gui fixture(*guiRuntime);
            fixture.AddPanel("RegressionPanel",VxColor(1,0,0,1),0,0,1,1);
            fixture.AddText("RegressionText","test",0,0,1,1);
            fixture.AddTextInput("RegressionInput",LegacyExecuteBB::GAMEFONT_01,0,0,1,1);
            auto keys=fixture.AddKeyButton("RegressionKey","key",0);
            keys.first->SetCallback([&](){++clicks;});keys.first->InvokeCallback();
            auto pair=fixture.AddYesNoButton("RegressionYesNo",0,0,0.5f,[&](bool){++clicks;});
            pair.first->InvokeCallback();pair.second->InvokeCallback();
            fixture.SetVisible(false);
        }
        Require(clicks==3,"callbacks");
        Require(LiveIds()==before,"normal destruction leaked or deleted other CK objects");
        Require(owner->GetLocalParameterCount()==parameters,"text source parameters leaked");
        auto *object=static_cast<CK2dEntity *>(ctx->CreateObject(CKCID_2DENTITY,"RegressionOld"));
        CK_ID id=object->GetID();auto stale=guiRuntime->Keep(object);
        ctx->DestroyObject(id,CK_DESTROY_FREEID);
        auto *replacement=static_cast<CK2dEntity *>(ctx->CreateObject(CKCID_2DENTITY,"RegressionReplacement"));
        Require(replacement->GetID()==id,"fixture did not actually reuse CK_ID");
        bool rejected=false;try{(void)stale.Get();}catch(const std::logic_error&){rejected=true;}
        Require(rejected,"stale reference rebound to recycled CK_ID");
        auto fresh=guiRuntime->Keep(replacement);Require(fresh.Get()==replacement,"fresh reference");
        stale.Destroy();Require(ctx->GetObject(id)==replacement,"stale destructor deleted replacement");
        fresh.Destroy();Require(LiveIds()==before,"replacement cleanup");
        GetLogger()->Info("GUI_LIFETIME_PASS callbacks=3 reused_id=%u source_parameters=%d",id,parameters);
    }
    void OnProcess() override {
        if (++frames!=3) return;
        const char *value=std::getenv("BALLANCE_E1697_RESTARTS_SENT");
        const int sent=value?std::atoi(value):0;
        if(sent<8) {
            char next[16];std::snprintf(next,sizeof(next),"%d",sent+1);
            Require(setenv("BALLANCE_E1697_RESTARTS_SENT",next,1)==0,"restart counter");
            const int queued=InterfaceManager::PostPlayerCommand(m_BML->GetCKContext(),TT_PLAYER_COMMAND_CMO_RESTART);
            GetLogger()->Info("GUI_RELOAD_QUEUED index=%d result=%d",sent+1,queued);
            Require(queued!=0,"queue reload");
        }
    }
    unsigned frames=0;
    LegacyTASGui::Layout *layout=nullptr;
    void OnUnload() override {
        delete layout;layout=nullptr;
        Segment::OnUnload();
        guiRuntime.reset();helpers.reset();
    }
    ~LegacySegmentProbe() override { OnUnload(); }
    std::unique_ptr<LegacyExecuteBB::Runtime> helpers;
    std::unique_ptr<LegacyBGui::Runtime> guiRuntime;
};
MOD_EXPORT IMod *BMLEntry(IBML *bml){return new LegacySegmentProbe(bml);}
MOD_EXPORT void BMLExit(IMod *mod){delete mod;}
