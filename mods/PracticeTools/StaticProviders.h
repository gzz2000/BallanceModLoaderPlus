#pragma once
#include <CKAnimationSnapshot.h>
#include <CKRenderCallbackSnapshot.h>
extern "C" {
int SDL_SoundCaptureSnapshot(CKContext *, void **);
int SDL_SoundPrepareRestore(CKContext *, void *, void **);
int SDL_SoundCommitRestore(CKContext *, void *);
void SDL_SoundDestroySnapshot(void *);
void SDL_SoundDestroyRestore(void *);
int BML_LegacySegmentCapture(CKContext *, LegacySegmentCheckpointV1 *);
int BML_LegacySegmentValidate(CKContext *, const LegacySegmentCheckpointV1 *);
int BML_LegacySegmentRestore(CKContext *, const LegacySegmentCheckpointV1 *);
int BML_TASOwnsKeyboard(CKInputManager *);
}
inline void *PracticeStaticSymbol(const char *name) {
    if (!name)
        return nullptr;
#define PRACTICE_SYMBOL(fn)                                                                        \
    if (std::strcmp(name, #fn) == 0)                                                               \
    return reinterpret_cast<void *>(&fn)
    PRACTICE_SYMBOL(CKCaptureAnimationState);
    PRACTICE_SYMBOL(CKCaptureEntity2dState);
    PRACTICE_SYMBOL(CKCaptureEntityTransformState);
    PRACTICE_SYMBOL(CKCaptureEntityUserBoxState);
    PRACTICE_SYMBOL(CKCaptureEntityVisibilityState);
    PRACTICE_SYMBOL(CKCaptureRenderCallbacks);
    PRACTICE_SYMBOL(CKCommitAnimationRestore);
    PRACTICE_SYMBOL(CKDestroyAnimationRestore);
    PRACTICE_SYMBOL(CKDestroyAnimationState);
    PRACTICE_SYMBOL(CKDestroyRenderCallbacks);
    PRACTICE_SYMBOL(CKPrepareAnimationRestore);
    PRACTICE_SYMBOL(CKRestoreEntity2dStates);
    PRACTICE_SYMBOL(CKRestoreEntityTransformState);
    PRACTICE_SYMBOL(CKRestoreEntityUserBoxState);
    PRACTICE_SYMBOL(CKRestoreEntityVisibilityState);
    PRACTICE_SYMBOL(CKRestoreRenderCallbacks);
    PRACTICE_SYMBOL(CKValidateRenderCallbacks);
    PRACTICE_SYMBOL(SDL_SoundCaptureSnapshot);
    PRACTICE_SYMBOL(SDL_SoundPrepareRestore);
    PRACTICE_SYMBOL(SDL_SoundCommitRestore);
    PRACTICE_SYMBOL(SDL_SoundDestroySnapshot);
    PRACTICE_SYMBOL(SDL_SoundDestroyRestore);
    PRACTICE_SYMBOL(BML_LegacySegmentCapture);
    PRACTICE_SYMBOL(BML_LegacySegmentValidate);
    PRACTICE_SYMBOL(BML_LegacySegmentRestore);
#undef PRACTICE_SYMBOL
    return nullptr;
}
