#include "BML/ScriptHelper.h"

#include "Behavior/Engine/Graph.h"
#include "CKBaseManager.h"
#include <stdexcept>


namespace {
// DeleteLink intentionally keeps graph links and redirects them to an inert
// behavior, matching the legacy API. The composition owns that behavior; a
// process-global pointer cannot survive ClearAll or dynamic-object deletion.
const CKGUID LinkSinkManagerGuid(0x6e7a4381, 0x2d056b94);
const CKGUID LinkSinkPrototypeGuid(0x302561c4, 0x0d282980);

class LinkSinkManager final : public CKBaseManager {
public:
    explicit LinkSinkManager(CKContext *context)
        : CKBaseManager(context, LinkSinkManagerGuid, (CKSTRING) "BML Link Sink") {}

    CKDWORD GetValidFunctionsMask() override {
        return CKMANAGER_FUNC_PreClearAll | CKMANAGER_FUNC_OnCKEnd |
               CKMANAGER_FUNC_OnSequenceToBeDeleted;
    }
    CKERROR PreClearAll() override { Reset(); return CK_OK; }
    CKERROR OnCKEnd() override { Reset(); return CK_OK; }
    CKERROR SequenceToBeDeleted(CK_ID *ids, int count) override {
        for (int i = 0; i < count; ++i) {
            if (ids[i] == m_Behavior || ids[i] == m_Input || ids[i] == m_Output) {
                Reset();
                break;
            }
        }
        return CK_OK;
    }

    CKBehavior *Get() {
        auto *object = m_Context->GetObject(m_Behavior);
        if (object && object->GetClassID() == CKCID_BEHAVIOR) {
            auto *behavior = static_cast<CKBehavior *>(object);
            if (behavior->GetPrototypeGuid() == LinkSinkPrototypeGuid &&
                behavior->GetInput(0) == m_Context->GetObject(m_Input) &&
                behavior->GetOutput(0) == m_Context->GetObject(m_Output) &&
                m_Input && m_Output)
                return behavior;
        }
        Reset();
        auto *behavior = static_cast<CKBehavior *>(m_Context->CreateObject(CKCID_BEHAVIOR));
        if (!behavior)
            throw std::runtime_error("Could not allocate the legacy link sink");
        if (behavior->InitFromGuid(LinkSinkPrototypeGuid) != CK_OK ||
            !behavior->GetInput(0) || !behavior->GetOutput(0)) {
            m_Context->DestroyObject(behavior);
            throw std::runtime_error("Legacy link sink prototype is unavailable");
        }
        m_Behavior = behavior->GetID();
        m_Input = behavior->GetInput(0)->GetID();
        m_Output = behavior->GetOutput(0)->GetID();
        return behavior;
    }

    static LinkSinkManager *ForContext(CKContext *context) {
        auto *manager = static_cast<LinkSinkManager *>(context->GetManagerByGuid(LinkSinkManagerGuid));
        if (!manager) {
            manager = new LinkSinkManager(context);
            if (context->RegisterNewManager(manager) != CK_OK)
                throw std::runtime_error("Could not register the legacy link sink manager");
        }
        return manager;
    }

private:
    void Reset() { m_Behavior = m_Input = m_Output = 0; }
    CK_ID m_Behavior = 0, m_Input = 0, m_Output = 0;
};
} // namespace

namespace ScriptHelper {
    bool FindBB(CKBehavior *script, std::function<bool(CKBehavior *)> callback, const char *name, bool hierarchically,
                int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        int cnt = script->GetSubBehaviorCount();
        for (int i = 0; i < cnt; i++) {
            CKBehavior *beh = script->GetSubBehavior(i);
            if (hierarchically && beh->GetSubBehaviorCount() > 0) {
                if (!FindBB(beh, callback, name, hierarchically, inputCnt, outputCnt, inputParamCnt, outputParamCnt))
                    return false;
            }

            if ((!name || !strcmp(beh->GetName(), name)) && (inputCnt < 0 || beh->GetInputCount() == inputCnt) &&
                (outputCnt < 0 || beh->GetOutputCount() == outputCnt) &&
                (inputParamCnt < 0 || beh->GetInputParameterCount() == inputParamCnt) &&
                (outputParamCnt < 0 || beh->GetOutputParameterCount() == outputParamCnt))
                if (!callback(beh))
                    return false;
        }

        return true;
    }

    CKBehavior *FindFirstBB(CKBehavior *script, const char *name, bool hierarchically,
                            int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        CKBehavior *res = nullptr;
        FindBB(
            script, [&res](CKBehavior *beh) {
                res = beh;
                return false;
            },
            name, hierarchically,
            inputCnt, outputCnt, inputParamCnt, outputParamCnt);
        return res;
    }

    CKBehaviorLink *CreateLink(CKBehavior *script, CKBehavior *inBeh, CKBehavior *outBeh,
                               int inPos, int outPos, int delay) {
        return CreateLink(script, inBeh->GetOutput(inPos), outBeh->GetInput(outPos), delay);
    }

    CKBehaviorLink *CreateLink(CKBehavior *script, CKBehavior *inBeh, CKBehaviorIO *out, int inPos, int delay) {
        return CreateLink(script, inBeh->GetOutput(inPos), out, delay);
    }

    CKBehaviorLink *CreateLink(CKBehavior *script, CKBehaviorIO *in, CKBehavior *outBeh, int outPos, int delay) {
        return CreateLink(script, in, outBeh->GetInput(outPos), delay);
    }

    CKBehaviorLink *CreateLink(CKBehavior *script, CKBehaviorIO *in, CKBehaviorIO *out, int delay) {
        CKBehaviorLink *link = (CKBehaviorLink *) script->GetCKContext()->CreateObject(CKCID_BEHAVIORLINK);
        link->SetInitialActivationDelay(delay);
        link->ResetActivationDelay();
        link->SetInBehaviorIO(in);
        link->SetOutBehaviorIO(out);
        script->AddSubBehaviorLink(link);
        return link;
    }

    CKBehavior *CreateBB(CKBehavior *script, CKGUID guid, bool target) {
        CKBehavior *beh = (CKBehavior *) script->GetCKContext()->CreateObject(CKCID_BEHAVIOR);
        beh->InitFromGuid(guid);
        if (target)
            beh->UseTarget();
        if (BML::Behavior::Internal::Engine::AddChild(script, beh) != CK_OK) {
            script->GetCKContext()->DestroyObject(beh);
            return nullptr;
        }
        return beh;
    }

    void InsertBB(CKBehavior *script, CKBehaviorLink *link, CKBehavior *beh, int inPos, int outPos) {
        CreateLink(script, beh, link->GetOutBehaviorIO(), outPos);
        link->SetOutBehaviorIO(beh->GetInput(inPos));
    }

    CKParameterLocal *CreateLocalParameter(CKBehavior *script, const char *name, CKGUID type) {
        return script->CreateLocalParameter((CKSTRING) name, type);
    }

    CKParameterLocal *CreateParamObject(CKBehavior *script, const char *name, CKGUID guid, CKObject *value) {
        return CreateParamValue<CK_ID>(script, name, guid, CKOBJID(value));
    }

    CKParameterLocal *CreateParamString(CKBehavior *script, const char *name, const char *value) {
        CKParameterLocal *param = CreateLocalParameter(script, name, CKPGUID_STRING);
        param->SetStringValue((CKSTRING) value);
        return param;
    }

    void SetParamObject(CKParameter *param, CKObject *value) {
        CK_ID obj = CKOBJID(value);
        SetParamValue(param, obj);
    }

    void SetParamString(CKParameter *param, const char *value) {
        param->SetStringValue((CKSTRING) value);
    }

    CKObject *GetParamObject(CKParameter *param) {
        return param->GetValueObject();
    }

    const char *GetParamString(CKParameter *param) {
        return static_cast<const char *>(param->GetReadDataPtr());
    }

    CKBehaviorLink *FindNextLink(CKBehavior *script, CKBehavior *beh, const char *name, int inPos, int outPos,
                                 int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        int linkCnt = script->GetSubBehaviorLinkCount();
        for (int i = 0; i < linkCnt; i++) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            CKBehaviorIO *in = link->GetInBehaviorIO();

            if (in->GetOwner() == beh && (inPos < 0 || beh->GetOutput(inPos) == in)) {
                CKBehaviorIO *out = link->GetOutBehaviorIO();
                CKBehavior *outBeh = out->GetOwner();

                if ((!name || !strcmp(outBeh->GetName(), name)) && (outPos < 0 || outBeh->GetInput(outPos) == out) &&
                    (inputCnt < 0 || outBeh->GetInputCount() == inputCnt) &&
                    (outputCnt < 0 || outBeh->GetOutputCount() == outputCnt) &&
                    (inputParamCnt < 0 || outBeh->GetInputParameterCount() == inputParamCnt) &&
                    (outputParamCnt < 0 || outBeh->GetOutputParameterCount() == outputParamCnt))
                    return link;
            }
        }

        return nullptr;
    }

    CKBehaviorLink *FindPreviousLink(CKBehavior *script, CKBehavior *beh, const char *name, int inPos, int outPos,
                                     int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        int linkCnt = script->GetSubBehaviorLinkCount();
        for (int i = 0; i < linkCnt; i++) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            CKBehaviorIO *out = link->GetOutBehaviorIO();

            if (out->GetOwner() == beh && (outPos < 0 || beh->GetInput(outPos) == out)) {
                CKBehaviorIO *in = link->GetInBehaviorIO();
                CKBehavior *inBeh = in->GetOwner();

                if ((!name || !strcmp(inBeh->GetName(), name)) && (inPos < 0 || inBeh->GetOutput(inPos) == in) &&
                    (inputCnt < 0 || inBeh->GetInputCount() == inputCnt) &&
                    (outputCnt < 0 || inBeh->GetOutputCount() == outputCnt) &&
                    (inputParamCnt < 0 || inBeh->GetInputParameterCount() == inputParamCnt) &&
                    (outputParamCnt < 0 || inBeh->GetOutputParameterCount() == outputParamCnt))
                    return link;
            }
        }

        return nullptr;
    }

    CKBehavior *FindNextBB(CKBehavior *script, CKBehavior *beh, const char *name, int inPos, int outPos,
                           int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        CKBehaviorLink *link = FindNextLink(script, beh, name, inPos, outPos, inputCnt, outputCnt, inputParamCnt,
                                            outputParamCnt);
        return link ? link->GetOutBehaviorIO()->GetOwner() : nullptr;
    }

    CKBehavior *FindPreviousBB(CKBehavior *script, CKBehavior *beh, const char *name, int inPos, int outPos,
                               int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        CKBehaviorLink *link = FindPreviousLink(script, beh, name, inPos, outPos, inputCnt, outputCnt, inputParamCnt,
                                                outputParamCnt);
        return link ? link->GetInBehaviorIO()->GetOwner() : nullptr;
    }

    CKBehaviorLink *FindNextLink(CKBehavior *script, CKBehaviorIO *io, const char *name, int outPos,
                                 int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        int linkCnt = script->GetSubBehaviorLinkCount();
        for (int i = 0; i < linkCnt; i++) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            CKBehaviorIO *in = link->GetInBehaviorIO();

            if (in == io) {
                CKBehaviorIO *out = link->GetOutBehaviorIO();
                CKBehavior *outBeh = out->GetOwner();

                if ((!name || !strcmp(outBeh->GetName(), name)) && (outPos < 0 || outBeh->GetInput(outPos) == out) &&
                    (inputCnt < 0 || outBeh->GetInputCount() == inputCnt) &&
                    (outputCnt < 0 || outBeh->GetOutputCount() == outputCnt) &&
                    (inputParamCnt < 0 || outBeh->GetInputParameterCount() == inputParamCnt) &&
                    (outputParamCnt < 0 || outBeh->GetOutputParameterCount() == outputParamCnt))
                    return link;
            }
        }

        return nullptr;
    }

    CKBehaviorLink *FindPreviousLink(CKBehavior *script, CKBehaviorIO *io, const char *name, int inPos,
                                     int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        int linkCnt = script->GetSubBehaviorLinkCount();
        for (int i = 0; i < linkCnt; i++) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            CKBehaviorIO *out = link->GetOutBehaviorIO();

            if (out == io) {
                CKBehaviorIO *in = link->GetInBehaviorIO();
                CKBehavior *inBeh = in->GetOwner();

                if ((!name || !strcmp(inBeh->GetName(), name)) && (inPos < 0 || inBeh->GetOutput(inPos) == in) &&
                    (inputCnt < 0 || inBeh->GetInputCount() == inputCnt) &&
                    (outputCnt < 0 || inBeh->GetOutputCount() == outputCnt) &&
                    (inputParamCnt < 0 || inBeh->GetInputParameterCount() == inputParamCnt) &&
                    (outputParamCnt < 0 || inBeh->GetOutputParameterCount() == outputParamCnt))
                    return link;
            }
        }

        return nullptr;
    }

    CKBehavior *FindNextBB(CKBehavior *script, CKBehaviorIO *io, const char *name, int outPos,
                           int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        CKBehaviorLink *link = FindNextLink(script, io, name, outPos, inputCnt, outputCnt, inputParamCnt,
                                            outputParamCnt);
        return link ? link->GetOutBehaviorIO()->GetOwner() : nullptr;
    }

    CKBehavior *FindPreviousBB(CKBehavior *script, CKBehaviorIO *io, const char *name, int inPos,
                               int inputCnt, int outputCnt, int inputParamCnt, int outputParamCnt) {
        CKBehaviorLink *link = FindPreviousLink(script, io, name, inPos, inputCnt, outputCnt, inputParamCnt,
                                                outputParamCnt);
        return link ? link->GetInBehaviorIO()->GetOwner() : nullptr;
    }

    CKBehavior *FindEndOfChain(CKBehavior *script, CKBehavior *beh) {
        CKBehavior *next;
        while (true) {
            next = FindNextBB(script, beh);
            if (next)
                beh = next;
            else
                break;
        };
        return beh;
    }

    void DeleteLink(CKBehavior *script, CKBehaviorLink *link) {
        if (!script || !link || script->GetCKContext() != link->GetCKContext())
            throw std::invalid_argument("DeleteLink requires objects from the same context");
        CKBehavior *nop = LinkSinkManager::ForContext(script->GetCKContext())->Get();
        link->SetInBehaviorIO(nop->GetOutput(0));
        link->SetOutBehaviorIO(nop->GetInput(0));
    }

    void DeleteBB(CKBehavior *script, CKBehavior *beh) {
        beh->Activate(false);
        int linkCnt = script->GetSubBehaviorLinkCount();
        for (int i = 0; i < linkCnt; i++) {
            CKBehaviorLink *link = script->GetSubBehaviorLink(i);
            if (link->GetInBehaviorIO()->GetOwner() == beh || link->GetOutBehaviorIO()->GetOwner() == beh)
                DeleteLink(script, link);
        }
    }
}
