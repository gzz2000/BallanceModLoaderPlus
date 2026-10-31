// Derived from BML 0.3.40 BMLMod::OnLoad (MIT; see LICENSE).
#include "AuxiliaryAssets.h"
#include <stdexcept>

namespace Legacy0340 {
void AuxiliaryAssets::Load(CKContext *context) {
    if(!context || travelCamera || physicalTemplates[0])
        throw std::logic_error("Legacy auxiliary assets require an unloaded holder and a live context");
    physicalTemplates[0]=static_cast<CK3dEntity *>(LegacyExecuteBB::ObjectLoad("3D Entities\\PH\\P_Ball_Paper.nmo",true,"P_Ball_Paper_MF").second);
    physicalTemplates[1]=static_cast<CK3dEntity *>(LegacyExecuteBB::ObjectLoad("3D Entities\\PH\\P_Ball_Wood.nmo",true,"P_Ball_Wood_MF").second);
    physicalTemplates[2]=static_cast<CK3dEntity *>(LegacyExecuteBB::ObjectLoad("3D Entities\\PH\\P_Ball_Stone.nmo",true,"P_Ball_Stone_MF").second);
    physicalTemplates[3]=static_cast<CK3dEntity *>(LegacyExecuteBB::ObjectLoad("3D Entities\\PH\\P_Box.nmo",true,"P_Box_MF").second);
    travelCamera=static_cast<CKCamera *>(context->CreateObject(CKCID_CAMERA,"TravelCam"));
    for(auto *object:physicalTemplates)
        if(!object)throw std::runtime_error("Legacy physical template failed to load");
    if(!travelCamera)throw std::runtime_error("Legacy travel camera failed to create");
}
}
