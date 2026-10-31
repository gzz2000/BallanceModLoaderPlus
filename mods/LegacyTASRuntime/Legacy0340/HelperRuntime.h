// Instance adaptation of BML 0.3.40 ExecuteBB (MIT; see LICENSE).
#pragma once
#include "ExecuteBB.h"
#include <vector>
namespace LegacyExecuteBB {
// The composition owns CK objects; this instance only borrows them. Its host
// must notify deletions and discard the instance before ClearAll/context end.
class Runtime {
public:
    explicit Runtime(CKContext *context) { Init(context); }
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    void Validate() const { EnsureLive(); }
    // Original BML 0.3.40 travel/physics templates, loaded once per world.
    // CK owns these objects. ClearAll destroys the holder; deletion notifications
    // invalidate it so an old ID cannot silently bind to a replacement object.
    void LoadAuxiliaryAssets();
    CK3dEntity *GetPhysicalTemplate(unsigned index) const;
    CKCamera *GetTravelCamera() const;
    void ObjectsToBeDeleted(const CK_ID *ids, int count) noexcept;
    const std::vector<CK_ID> &CreatedObjectIds() const { return m_CreatedIds; }
	void InitFont(FontType type, int fontIndex);
	int GetFont(FontType type);
	FontType GetFontType(int font);

	void Init(CKContext* context);



	CKBehavior* Create2DText(CK2dEntity* target = nullptr, FontType font = NOFONT, CKSTRING text = "", int align = ALIGN_CENTER,
		VxRect margin = { 2, 2, 2, 2 }, Vx2DVector offset = { 0, 0 }, Vx2DVector pindent = { 0, 0 }, CKMaterial* bgmat = nullptr,
		float caretsize = 0.1f, CKMaterial* caretmat = nullptr, int flags = TEXT_SCREEN);

	CKBehavior* CreatePhysicalizeConvex(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(), CKMesh* mesh = nullptr);

	CKBehavior* CreatePhysicalizeBall(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(),
		VxVector ballCenter = VxVector(), float ballRadius = 2.0f);

	CKBehavior* CreatePhysicalizeConcave(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(), CKMesh* mesh = nullptr);

	void PhysicalizeConvex(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(), CKMesh* mesh = nullptr);

	void PhysicalizeBall(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(),
		VxVector ballCenter = VxVector(), float ballRadius = 2.0f);

	void PhysicalizeConcave(CK3dEntity* target = nullptr, CKBOOL fixed = false, float friction = 0.7f, float elasticity = 0.4f,
		float mass = 1.0f, CKSTRING collGroup = "", CKBOOL startFrozen = false, CKBOOL enableColl = true, CKBOOL calcMassCenter = false,
		float linearDamp = 0.1f, float rotDamp = 0.1f, CKSTRING collSurface = "", VxVector massCenter = VxVector(), CKMesh* mesh = nullptr);

	void Unphysicalize(CK3dEntity* target);

	CKBehavior* CreateObjectLoad(CKSTRING file = "", CKSTRING mastername = "", CK_CLASSID filter = CKCID_3DOBJECT,
		CKBOOL addToScene = true, CKBOOL reuseMesh = true, CKBOOL reuseMtl = true, CKBOOL dynamic = true);

	::std::pair<XObjectArray*, CKObject*> ObjectLoad(CKSTRING file = "", bool rename = true, CKSTRING mastername = "",
		CK_CLASSID filter = CKCID_3DOBJECT, CKBOOL addToScene = true, CKBOOL reuseMesh = true, CKBOOL reuseMtl = true, CKBOOL dynamic = true);

	CKBehavior* CreatePhysicsImpulse(CK3dEntity* target = nullptr, VxVector position = VxVector(), CK3dEntity* posRef = nullptr,
		VxVector direction = VxVector(), CK3dEntity* dirRef = nullptr, float impulse = 0.0f);

	void PhysicsImpulse(CK3dEntity* target = nullptr, VxVector position = VxVector(), CK3dEntity* posRef = nullptr,
		VxVector direction = VxVector(), CK3dEntity* dirRef = nullptr, float impulse = 0.0f);
private:
    void EnsureLive() const;
    CKBehavior *MakeBehavior(CKBehavior *owner, CKGUID guid, bool target = false);
    template<typename T> CKParameter *CreateParamValue(CKGUID guid, CKSTRING name, T value);
    CKParameter *CreateParamObject(CKGUID guid, CKSTRING name, CKObject *value);
    CKParameter *CreateParamString(CKSTRING name, CKSTRING value);
    CKBehavior *CreatePhysicalize(CK3dEntity *target, CKBOOL fixed, float friction,
        float elasticity, float mass, CKSTRING collGroup, CKBOOL startFrozen,
        CKBOOL enableColl, CKBOOL calcMassCenter, float linearDamp, float rotDamp,
        CKSTRING collSurface, VxVector massCenter);
    void PhysicalizeParam(CKBehavior *beh, CK3dEntity *target, CKBOOL fixed,
        float friction, float elasticity, float mass, CKSTRING collGroup,
        CKBOOL startFrozen, CKBOOL enableColl, CKBOOL calcMassCenter,
        float linearDamp, float rotDamp, CKSTRING collSurface, VxVector massCenter);
    CKContext *m_Context = nullptr;
    CK_ID m_OwnerId = 0;
    CK_ID m_HelperIds[5] = {};
    bool m_Valid = false;
    bool m_Constructing = false;
    int m_LoadRenameCount = 0;
    bool m_AssetsLoaded = false;
    CK_ID m_AssetIds[5] = {};
    CK3dEntity *m_PhysicalTemplates[4] = {};
    CKCamera *m_TravelCamera = nullptr;
    std::vector<CK_ID> m_CreatedIds;
    int GameFonts[8] = {};
    CKBehavior *ownerScript = nullptr;
    CKBehavior *bbPhysConv = nullptr, *bbPhysBall = nullptr, *bbPhysConc = nullptr;
    CKBehavior *bbObjLoad = nullptr, *bbPhyImpul = nullptr;

};
}
