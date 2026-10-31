#include "HelperRuntime.h"
#include <algorithm>
#include <set>
#include "ScriptHelper.h"
#include "RegisterBB.h"
#include <stdexcept>

using namespace LegacyScriptHelper;

namespace LegacyExecuteBB {
	void Runtime::InitFont(FontType type, int fontIndex) {
        EnsureLive();
        if (type < NOFONT || type > GAMEFONT_CREDITS_BIG) throw std::out_of_range("legacy font index");
		GameFonts[type] = fontIndex;
	}
	int Runtime::GetFont(FontType type) {
        EnsureLive();
        if (type < NOFONT || type > GAMEFONT_CREDITS_BIG) throw std::out_of_range("legacy font index");
		return GameFonts[type];
	}
	FontType Runtime::GetFontType(int font) {
        EnsureLive();
		for (int i = 0; i < 8; i++)
			if (GameFonts[i] == font)
				return static_cast<FontType>(i);
		return NOFONT;
	}

	template<typename T>
	CKParameter* Runtime::CreateParamValue(CKGUID guid, CKSTRING name, T value) {
        EnsureLive();
		CKParameter* param = CreateLocalParameter(ownerScript, name, guid);
		if (!param) throw std::runtime_error("legacy parameter allocation failed");
		param->SetValue(&value, sizeof(T));
		return param;
	}

	CKParameter* Runtime::CreateParamObject(CKGUID guid, CKSTRING name, CKObject* value) {
		return CreateParamValue<CK_ID>(guid, name, CKOBJID(value));
	}

	CKParameter* Runtime::CreateParamString(CKSTRING name, CKSTRING value) {
        EnsureLive();
		CKParameter* param = CreateLocalParameter(ownerScript, name, CKPGUID_STRING);
		if (!param) throw std::runtime_error("legacy string parameter allocation failed");
		param->SetStringValue(value);
		return param;
	}

	CKBehavior* Runtime::Create2DText(CK2dEntity* target, FontType font, CKSTRING text, int align, VxRect margin, Vx2DVector offset,
		Vx2DVector pindent, CKMaterial* bgmat, float caretsize, CKMaterial* caretmat, int flags) {
		CKBehavior* beh = MakeBehavior(ownerScript, VT_TEXT2D, true);
		beh->GetTargetParameter()->SetDirectSource(CreateParamObject(CKPGUID_2DENTITY, "Target", target));
		beh->GetInputParameter(0)->SetDirectSource(CreateParamValue(CKPGUID_FONT, "Font", GetFont(font)));
		beh->GetInputParameter(1)->SetDirectSource(CreateParamString("Text", text));
		beh->GetInputParameter(2)->SetDirectSource(CreateParamValue(CKPGUID_ALIGNMENT, "Alignment", align));
		beh->GetInputParameter(3)->SetDirectSource(CreateParamValue(CKPGUID_RECT, "Margins", margin));
		beh->GetInputParameter(4)->SetDirectSource(CreateParamValue(CKPGUID_2DVECTOR, "Offset", offset));
		beh->GetInputParameter(5)->SetDirectSource(CreateParamValue(CKPGUID_2DVECTOR, "Paragraph Indentation", pindent));
		beh->GetInputParameter(6)->SetDirectSource(CreateParamObject(CKPGUID_MATERIAL, "Background Material", bgmat));
		beh->GetInputParameter(7)->SetDirectSource(CreateParamValue(CKPGUID_PERCENTAGE, "Caret Size", caretsize));
		beh->GetInputParameter(8)->SetDirectSource(CreateParamObject(CKPGUID_MATERIAL, "Caret Material", caretmat));
		SetParamValue(beh->GetLocalParameter(0), flags);
		return beh;
	}

	CKBehavior* Runtime::CreatePhysicalize(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter) {
		CKBehavior* beh = MakeBehavior(ownerScript, TT_PHYSICALIZE, true);
		beh->GetTargetParameter()->SetDirectSource(CreateParamObject(CKPGUID_3DENTITY, "Target", target));
		beh->GetInputParameter(0)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Fixed", fixed));
		beh->GetInputParameter(1)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Friction", friction));
		beh->GetInputParameter(2)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Elasticity", elasticity));
		beh->GetInputParameter(3)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Mass", mass));
		beh->GetInputParameter(4)->SetDirectSource(CreateParamString("Collision Group", collGroup));
		beh->GetInputParameter(5)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Start Frozen", startFrozen));
		beh->GetInputParameter(6)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Enable Collision", enableColl));
		beh->GetInputParameter(7)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Calculate Mass Center", calcMassCenter));
		beh->GetInputParameter(8)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Linear Speed Damp", linearDamp));
		beh->GetInputParameter(9)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Rot Speed Damp", rotDamp));
		beh->GetInputParameter(10)->SetDirectSource(CreateParamString("Collision Surface", collSurface));
		SetParamValue(beh->GetLocalParameter(3), massCenter);
		return beh;
	}

	CKBehavior* Runtime::CreatePhysicalizeConvex(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, CKMesh* mesh) {
		CKBehavior* beh = CreatePhysicalize(target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		beh->GetInputParameter(11)->SetDirectSource(CreateParamObject(CKPGUID_MESH, "Mesh", mesh));
		return beh;
	}

	CKBehavior* Runtime::CreatePhysicalizeBall(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, VxVector ballCenter, float ballRadius) {
		CKBehavior* beh = CreatePhysicalize(target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		SetParamValue(beh->GetLocalParameter(0), 0);
		SetParamValue(beh->GetLocalParameter(1), 1);
		beh->CallCallbackFunction(CKM_BEHAVIORSETTINGSEDITED);
		beh->GetInputParameter(11)->SetDirectSource(CreateParamValue(CKPGUID_VECTOR, "Ball Position", ballCenter));
		beh->GetInputParameter(12)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Ball Radius", ballRadius));
		return beh;
	}

	CKBehavior* Runtime::CreatePhysicalizeConcave(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, CKMesh* mesh) {
		CKBehavior* beh = CreatePhysicalize(target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		SetParamValue(beh->GetLocalParameter(0), 0);
		SetParamValue(beh->GetLocalParameter(2), 1);
		beh->CallCallbackFunction(CKM_BEHAVIORSETTINGSEDITED);
		beh->GetInputParameter(11)->SetDirectSource(CreateParamObject(CKPGUID_MESH, "Mesh", mesh));
		return beh;
	}

	void Runtime::PhysicalizeParam(CKBehavior* beh, CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter) {
        EnsureLive();
		SetParamObject(beh->GetTargetParameter()->GetDirectSource(), target);
		SetParamValue(beh->GetInputParameter(0)->GetDirectSource(), fixed);
		SetParamValue(beh->GetInputParameter(1)->GetDirectSource(), friction);
		SetParamValue(beh->GetInputParameter(2)->GetDirectSource(), elasticity);
		SetParamValue(beh->GetInputParameter(3)->GetDirectSource(), mass);
		SetParamString(beh->GetInputParameter(4)->GetDirectSource(), collGroup);
		SetParamValue(beh->GetInputParameter(5)->GetDirectSource(), startFrozen);
		SetParamValue(beh->GetInputParameter(6)->GetDirectSource(), enableColl);
		SetParamValue(beh->GetInputParameter(7)->GetDirectSource(), calcMassCenter);
		SetParamValue(beh->GetInputParameter(8)->GetDirectSource(), linearDamp);
		SetParamValue(beh->GetInputParameter(9)->GetDirectSource(), rotDamp);
		SetParamString(beh->GetInputParameter(10)->GetDirectSource(), collSurface);
		SetParamValue(beh->GetLocalParameter(3), massCenter);
	}

	void Runtime::PhysicalizeConvex(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, CKMesh* mesh) {
		PhysicalizeParam(bbPhysConv, target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		SetParamObject(bbPhysConv->GetInputParameter(11)->GetDirectSource(), mesh);
		bbPhysConv->ActivateInput(0);
		bbPhysConv->Execute(0);
	}

	void Runtime::PhysicalizeBall(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, VxVector ballCenter, float ballRadius) {
		PhysicalizeParam(bbPhysBall, target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		SetParamValue(bbPhysBall->GetInputParameter(11)->GetDirectSource(), ballCenter);
		SetParamValue(bbPhysBall->GetInputParameter(12)->GetDirectSource(), ballRadius);
		bbPhysBall->ActivateInput(0);
		bbPhysBall->Execute(0);
	}

	void Runtime::PhysicalizeConcave(CK3dEntity* target, CKBOOL fixed, float friction, float elasticity, float mass,
		CKSTRING collGroup, CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp,
		float rotDamp, CKSTRING collSurface, VxVector massCenter, CKMesh* mesh) {
		PhysicalizeParam(bbPhysConc, target, fixed, friction, elasticity, mass, collGroup, startFrozen,
			enableColl, calcMassCenter, linearDamp, rotDamp, collSurface, massCenter);
		SetParamObject(bbPhysConc->GetInputParameter(11)->GetDirectSource(), mesh);
		bbPhysConc->ActivateInput(0);
		bbPhysConc->Execute(0);
	}

	void Runtime::Unphysicalize(CK3dEntity* target) {
        EnsureLive();
		SetParamObject(bbPhysConv->GetTargetParameter()->GetDirectSource(), target);
		bbPhysConv->ActivateInput(1);
		bbPhysConv->Execute(0);
	}

	CKBehavior* Runtime::CreateObjectLoad(CKSTRING file, CKSTRING mastername, CK_CLASSID filter, CKBOOL addToScene,
		CKBOOL reuseMesh, CKBOOL reuseMtl, CKBOOL dynamic) {
		CKBehavior* beh = MakeBehavior(ownerScript, VT_OBJECTLOAD);
		beh->GetInputParameter(0)->SetDirectSource(CreateParamString("File", file));
		beh->GetInputParameter(1)->SetDirectSource(CreateParamString("Master Name", mastername));
		beh->GetInputParameter(2)->SetDirectSource(CreateParamValue(CKPGUID_CLASSID, "Filter", filter));
		beh->GetInputParameter(3)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Add to Scene", addToScene));
		beh->GetInputParameter(4)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Reuse Mesh", reuseMesh));
		beh->GetInputParameter(5)->SetDirectSource(CreateParamValue(CKPGUID_BOOL, "Reuse Material", reuseMtl));
		SetParamValue(beh->GetLocalParameter(0), dynamic);
		return beh;
	}

	std::pair<XObjectArray*, CKObject*> Runtime::ObjectLoad(CKSTRING file, bool rename, CKSTRING mastername, CK_CLASSID filter,
		CKBOOL addToScene, CKBOOL reuseMesh, CKBOOL reuseMtl, CKBOOL dynamic) {
        EnsureLive();
		SetParamString(bbObjLoad->GetInputParameter(0)->GetDirectSource(), file);
		SetParamString(bbObjLoad->GetInputParameter(1)->GetDirectSource(), mastername);
		SetParamValue(bbObjLoad->GetInputParameter(2)->GetDirectSource(), filter);
		SetParamValue(bbObjLoad->GetInputParameter(3)->GetDirectSource(), addToScene);
		SetParamValue(bbObjLoad->GetInputParameter(4)->GetDirectSource(), reuseMesh);
		SetParamValue(bbObjLoad->GetInputParameter(5)->GetDirectSource(), reuseMtl);
		SetParamValue(bbObjLoad->GetLocalParameter(0), dynamic);
		bbObjLoad->ActivateInput(0);
		bbObjLoad->Execute(0);

		XObjectArray* array = *static_cast<XObjectArray**>(bbObjLoad->GetOutputParameterWriteDataPtr(0));
		if (!array) throw std::runtime_error("legacy ObjectLoad returned no array");
		if (rename) {
			++m_LoadRenameCount;
			for (CK_ID* id = array->Begin(); id != array->End(); id++) {
				CKObject* obj = bbObjLoad->GetCKContext()->GetObject(*id);
				if (CKIsChildClassOf(obj, CKCID_BEOBJECT)) {
					auto name = obj->GetName();
					obj->SetName((obj->GetName() + std::string("_BMLLoad_") + std::to_string(m_LoadRenameCount)).c_str());
				}
			}
		}

		return { array, bbObjLoad->GetOutputParameterObject(1) };
	}

	CKBehavior* Runtime::CreatePhysicsImpulse(CK3dEntity* target, VxVector position, CK3dEntity* posRef, VxVector direction,
		CK3dEntity* dirRef, float impulse) {
		CKBehavior* beh = MakeBehavior(ownerScript, TT_PHYSICSIMPLUSE, true);
		beh->GetTargetParameter()->SetDirectSource(CreateParamObject(CKPGUID_3DENTITY, "Target", target));
		beh->GetInputParameter(0)->SetDirectSource(CreateParamValue(CKPGUID_VECTOR, "Position", position));
		beh->GetInputParameter(1)->SetDirectSource(CreateParamObject(CKPGUID_3DENTITY, "PosRef", posRef));
		beh->GetInputParameter(2)->SetDirectSource(CreateParamValue(CKPGUID_VECTOR, "Direction", direction));
		beh->GetInputParameter(3)->SetDirectSource(CreateParamObject(CKPGUID_3DENTITY, "DirRef", dirRef));
		beh->GetInputParameter(4)->SetDirectSource(CreateParamValue(CKPGUID_FLOAT, "Impulse", impulse));
		return beh;
	}

	void Runtime::PhysicsImpulse(CK3dEntity* target, VxVector position, CK3dEntity* posRef, VxVector direction,
		CK3dEntity* dirRef, float impulse) {
        EnsureLive();
		SetParamObject(bbPhyImpul->GetTargetParameter()->GetDirectSource(), target);
		SetParamValue(bbPhyImpul->GetInputParameter(0)->GetDirectSource(), position);
		SetParamObject(bbPhyImpul->GetInputParameter(1)->GetDirectSource(), posRef);
		SetParamValue(bbPhyImpul->GetInputParameter(2)->GetDirectSource(), direction);
		SetParamObject(bbPhyImpul->GetInputParameter(3)->GetDirectSource(), dirRef);
		SetParamValue(bbPhyImpul->GetInputParameter(4)->GetDirectSource(), impulse);
		bbPhyImpul->ActivateInput(0);
		bbPhyImpul->Execute(0);
	}

    void Runtime::LoadAuxiliaryAssets() {
        EnsureLive();
        if (m_AssetsLoaded) return; // Validate above; never duplicate a live world.
        std::set<CK_ID> existing;
        for (int id=1;id<=m_Context->GetObjectCount();++id)
            if (m_Context->GetObject(id)) existing.insert(id);
        try {
            // Preserve the original BMLMod::OnLoad call and rename order.
            const char *files[] = {"3D Entities\\PH\\P_Ball_Paper.nmo",
                "3D Entities\\PH\\P_Ball_Wood.nmo", "3D Entities\\PH\\P_Ball_Stone.nmo",
                "3D Entities\\PH\\P_Box.nmo"};
            const char *masters[] = {"P_Ball_Paper_MF", "P_Ball_Wood_MF",
                "P_Ball_Stone_MF", "P_Box_MF"};
            for (int i=0;i<4;++i) {
                auto *object=ObjectLoad(files[i],true,masters[i]).second;
                if (!object || !CKIsChildClassOf(object,CKCID_3DENTITY))
                    throw std::runtime_error("legacy physical template failed to load");
                m_PhysicalTemplates[i]=static_cast<CK3dEntity *>(object);
                m_AssetIds[i]=object->GetID();
            }
            m_TravelCamera=static_cast<CKCamera *>(m_Context->CreateObject(CKCID_CAMERA,"TravelCam"));
            if (!m_TravelCamera) throw std::runtime_error("legacy travel camera failed to create");
            m_AssetIds[4]=m_TravelCamera->GetID();
            for (int id=1;id<=m_Context->GetObjectCount();++id)
                if (m_Context->GetObject(id) && !existing.count(id)) m_CreatedIds.push_back(id);
            std::sort(m_CreatedIds.begin(),m_CreatedIds.end());
            m_AssetsLoaded=true;
        } catch (...) {
            m_Valid=false; // Host aborts bootstrap and clears partially loaded world.
            throw;
        }
    }

    CK3dEntity *Runtime::GetPhysicalTemplate(unsigned index) const {
        EnsureLive();
        if (!m_AssetsLoaded || index>=4) throw std::out_of_range("legacy physical template");
        return m_PhysicalTemplates[index];
    }

    CKCamera *Runtime::GetTravelCamera() const {
        EnsureLive();
        if (!m_AssetsLoaded) throw std::logic_error("legacy assets are not loaded");
        return m_TravelCamera;
    }

    void Runtime::EnsureLive() const {
        if (!m_Context || (!m_Valid && !m_Constructing) ||
            m_Context->GetObject(m_OwnerId) != ownerScript)
            throw std::logic_error("legacy helper runtime is invalid");
        if (m_AssetsLoaded) {
            for (int i=0;i<4;++i)
                if (m_Context->GetObject(m_AssetIds[i]) != m_PhysicalTemplates[i])
                    throw std::logic_error("legacy physical template no longer exists");
            if (m_Context->GetObject(m_AssetIds[4]) != m_TravelCamera)
                throw std::logic_error("legacy travel camera no longer exists");
        }
        if (!m_Constructing) {
            CKBehavior *helpers[] = {bbPhysConv, bbPhysBall, bbPhysConc, bbObjLoad, bbPhyImpul};
            for (int i=0;i<5;++i)
                if (m_Context->GetObject(m_HelperIds[i]) != helpers[i])
                    throw std::logic_error("legacy cached behavior no longer exists");
        }
    }

    CKBehavior *Runtime::MakeBehavior(CKBehavior *owner, CKGUID guid, bool target) {
        EnsureLive();
        auto *behavior = static_cast<CKBehavior *>(m_Context->CreateObject(CKCID_BEHAVIOR));
        if (!behavior || behavior->InitFromGuid(guid) != CK_OK)
            throw std::runtime_error("legacy behavior prototype unavailable");
        if (target) behavior->UseTarget();
        if (owner->AddSubBehavior(behavior) != CK_OK)
            throw std::runtime_error("legacy helper attachment failed");
        return behavior;
    }

    void Runtime::ObjectsToBeDeleted(const CK_ID *ids, int count) noexcept {
        if (!ids || count <= 0) return;
        for (int i=0;i<count;++i) {
            if (ids[i] == m_OwnerId || std::binary_search(m_CreatedIds.begin(),m_CreatedIds.end(),ids[i])) {
                m_Valid = false;
                return;
            }
        }
    }

    void Runtime::Init(CKContext *context) {
        if (!context || m_Context)
            throw std::logic_error("legacy helper runtime requires a fresh instance and live context");
        m_Context=context;
        ownerScript=static_cast<CKBehavior *>(context->GetObjectByNameAndClass("Level_Init",CKCID_BEHAVIOR));
        if (!ownerScript) throw std::runtime_error("missing Level_Init");
        m_OwnerId=ownerScript->GetID();
        std::set<CK_ID> existing;
        for(int id=1;id<=context->GetObjectCount();++id)
            if(context->GetObject(id))existing.insert(id);
        for(int i=0;i<8;++i)GameFonts[i]=i;
        m_Constructing=true;
        try {
            bbPhysConv=CreatePhysicalizeConvex();
            bbPhysBall=CreatePhysicalizeBall();
            bbPhysConc=CreatePhysicalizeConcave();
            bbObjLoad=CreateObjectLoad();
            bbPhyImpul=CreatePhysicsImpulse();
            CKBehavior *helpers[]={bbPhysConv,bbPhysBall,bbPhysConc,bbObjLoad,bbPhyImpul};
            for(int i=0;i<5;++i)m_HelperIds[i]=helpers[i]->GetID();
            for(int id=1;id<=context->GetObjectCount();++id)
                if(context->GetObject(id) && !existing.count(id))m_CreatedIds.push_back(id);
            m_Valid=true;
        } catch (...) {
            m_Constructing=false;
            throw; // Host must abort this bootstrap and clear the composition.
        }
        m_Constructing=false;
    }
}
