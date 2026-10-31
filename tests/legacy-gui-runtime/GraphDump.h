#pragma once
#include <string>
#include <cstdio>
#include <cstring>
template<class Logger> void DumpLegacyGraph(Logger *log, CKContext *ctx, int base) {
    auto ref=[base](CKObject *o) -> std::string {
        if(!o)return "null";
        if(int(o->GetID())>base)return "new:"+std::to_string(int(o->GetID())-base);
        return "external:"+std::to_string(o->GetClassID())+":"+(o->GetName()?o->GetName():"<null>");
    };
    auto bytes=[](const void *p,int size) {
        if(!p)return std::string("null");
        if(size<0||size>4096)return std::string("ERROR_SIZE");
        std::string s;char b[3];auto *data=static_cast<const unsigned char *>(p);
        for(int i=0;i<size;++i){std::snprintf(b,sizeof(b),"%02x",unsigned(data[i]));s+=b;}
        return s;
    };
    auto bits=[](float f){unsigned n;static_assert(sizeof(n)==sizeof(f));std::memcpy(&n,&f,sizeof(n));return n;};
    for(int id=base+1;id<=ctx->GetObjectCount();++id) {
#ifdef _WIN32
        using Lookup=CKObject *(__thiscall *)(CKContext *,CK_ID);
        auto lookup=reinterpret_cast<Lookup>(GetProcAddress(GetModuleHandleA("CK2.dll"),"?GetObjectA@CKContext@@QAEPAVCKObject@@K@Z"));
        auto *o=lookup(ctx,id);
#else
        auto *o=ctx->GetObject(id);
#endif
        int slot=id-base;
        if(!o){log->Info("GUI_GRAPH slot=%d missing",slot);continue;}
        log->Info("GUI_GRAPH slot=%d object class=%d flags=%08x name=%s",slot,o->GetClassID(),unsigned(o->GetObjectFlags()),o->GetName()?o->GetName():"<null>");
        if(o->GetClassID()==CKCID_BEHAVIOR) {
            auto *b=static_cast<CKBehavior *>(o);auto g=b->GetPrototypeGuid();
            log->Info("GUI_GRAPH slot=%d behavior guid=%08x:%08x flags=%08x owner=%s parent=%s script=%s target=%s",slot,unsigned(g.d1),unsigned(g.d2),unsigned(b->GetFlags()),ref(b->GetOwner()).c_str(),ref(b->GetParent()).c_str(),ref(b->GetOwnerScript()).c_str(),ref(b->GetTargetParameter()).c_str());
            for(int i=0;i<b->GetInputCount();++i)log->Info("GUI_GRAPH slot=%d input index=%d ref=%s",slot,i,ref(b->GetInput(i)).c_str());
            for(int i=0;i<b->GetOutputCount();++i)log->Info("GUI_GRAPH slot=%d output index=%d ref=%s",slot,i,ref(b->GetOutput(i)).c_str());
            for(int i=0;i<b->GetInputParameterCount();++i)log->Info("GUI_GRAPH slot=%d pin index=%d ref=%s",slot,i,ref(b->GetInputParameter(i)).c_str());
            for(int i=0;i<b->GetOutputParameterCount();++i)log->Info("GUI_GRAPH slot=%d pout index=%d ref=%s",slot,i,ref(b->GetOutputParameter(i)).c_str());
            for(int i=0;i<b->GetLocalParameterCount();++i)log->Info("GUI_GRAPH slot=%d local index=%d ref=%s",slot,i,ref(b->GetLocalParameter(i)).c_str());
        }
        if(o->GetClassID()==CKCID_BEHAVIORIO) {
            auto *b=static_cast<CKBehaviorIO *>(o);
            log->Info("GUI_GRAPH slot=%d io owner=%s type=%d active=%d",slot,ref(b->GetOwner()).c_str(),b->GetType(),b->IsActive());
        }
        if(o->GetClassID()==CKCID_PARAMETERIN) {
            auto *p=static_cast<CKParameterIn *>(o);auto g=p->GetGUID();
            log->Info("GUI_GRAPH slot=%d pin guid=%08x:%08x owner=%s direct=%s shared=%s",slot,unsigned(g.d1),unsigned(g.d2),ref(p->GetOwner()).c_str(),ref(p->GetDirectSource()).c_str(),ref(p->GetSharedSource()).c_str());
        }
        if(o->GetClassID()==CKCID_PARAMETERLOCAL||o->GetClassID()==CKCID_PARAMETEROUT||o->GetClassID()==CKCID_PARAMETER) {
            auto *p=static_cast<CKParameter *>(o);auto g=p->GetGUID();int cid=p->GetParameterClassID();
            auto data=cid?ref(p->GetValueObject(FALSE)):bytes(p->GetReadDataPtr(FALSE),p->GetDataSize());
            log->Info("GUI_GRAPH slot=%d param guid=%08x:%08x owner=%s size=%d cid=%d data=%s",slot,unsigned(g.d1),unsigned(g.d2),ref(p->GetOwner()).c_str(),p->GetDataSize(),cid,data.c_str());
            if(o->GetClassID()==CKCID_PARAMETEROUT) {
                auto *q=static_cast<CKParameterOut *>(o);
                for(int i=0;i<q->GetDestinationCount();++i)log->Info("GUI_GRAPH slot=%d dest index=%d ref=%s",slot,i,ref(q->GetDestination(i)).c_str());
            }
        }
        if(o->GetClassID()==CKCID_2DENTITY||o->GetClassID()==CKCID_SPRITETEXT) {
            auto *e=static_cast<CK2dEntity *>(o);Vx2DVector pos,size;VxRect src;
            e->GetPosition(pos,TRUE);e->GetSize(size,TRUE);e->GetSourceRect(src);
            log->Info("GUI_GRAPH slot=%d entity flags=%08x parent=%s mat=%s z=%d pos=%08x:%08x size=%08x:%08x src=%08x:%08x:%08x:%08x",slot,unsigned(e->GetFlags()),ref(e->GetParent()).c_str(),ref(e->GetMaterial()).c_str(),e->GetZOrder(),bits(pos.x),bits(pos.y),bits(size.x),bits(size.y),bits(src.left),bits(src.top),bits(src.right),bits(src.bottom));
        }
        if(o->GetClassID()==CKCID_MATERIAL) {
            auto *m=static_cast<CKMaterial *>(o);auto c=m->GetDiffuse();
            log->Info("GUI_GRAPH slot=%d material diffuse=%08x:%08x:%08x:%08x texture=%s",slot,bits(c.r),bits(c.g),bits(c.b),bits(c.a),ref(m->GetTexture()).c_str());
        }
        if(o->GetClassID()==CKCID_SPRITETEXT) {
            auto *s=static_cast<CKSpriteText *>(o);
            log->Info("GUI_GRAPH slot=%d sprite text=%s color=%08x align=%d",slot,s->GetText()?s->GetText():"<null>",unsigned(s->GetTextColor()),int(s->GetAlign()));
        }
    }
    log->Info("GUI_GRAPH_COMPLETE slots=%d",ctx->GetObjectCount()-base);
}
