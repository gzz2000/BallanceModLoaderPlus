#pragma once
#include <CKAll.h>
namespace LegacyScriptHelper {
inline CKBehavior *CreateBB(CKBehavior *script,CKGUID guid,bool target=false) {
    auto *behavior=static_cast<CKBehavior *>(script->GetCKContext()->CreateObject(CKCID_BEHAVIOR));
    behavior->InitFromGuid(guid);
    if(target)behavior->UseTarget();
    script->AddSubBehavior(behavior);return behavior;
}
inline CKParameterLocal *CreateLocalParameter(CKBehavior *script,CKSTRING name,CKGUID type) {
    return script->CreateLocalParameter(name,type);
}
template<class T> void SetParamValue(CKParameter *param,T value) {param->SetValue(&value,sizeof(T));}
inline void SetParamObject(CKParameter *param,CKObject *value) {CK_ID id=CKOBJID(value);SetParamValue(param,id);}
inline void SetParamString(CKParameter *param,CKSTRING value) {param->SetStringValue(value);}
}
