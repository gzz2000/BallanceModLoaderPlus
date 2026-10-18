#include "UI/Ime/Tsf.h"

namespace Overlay::Ime::Tsf {
void Attach() {}
void Detach() {}
void SetPresentationOwned(bool) noexcept {}
bool IsAttached() noexcept { return false; }
void ClearCandidates() {}
std::uint64_t Revision() noexcept { return 0; }
bool HasCandidates() noexcept { return false; }
std::uint64_t ApplyCandidates(Snapshot &) { return 0; }
bool MoveSelection(CandidateDirection) { return false; }
}
