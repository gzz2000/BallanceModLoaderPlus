// Real-CK2 regression for legacy DeleteLink cache lifetime. Link with BML, CK2
// and VxMath; run separately with "contexts" and "reload" arguments.
#include "CKAll.h"
#include "BML/ScriptHelper.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
const CKGUID NopGuid(0x302561c4, 0x0d282980);
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
int Run(const CKBehaviorContext &) { return CKBR_OK; }
CKERROR Prototype(CKBehaviorPrototype **out) {
    *out = CreateCKBehaviorPrototype("Nop");
    (*out)->DeclareInput("In");
    (*out)->DeclareOutput("Out");
    (*out)->SetFunction(Run);
    return CK_OK;
}
void Register(XObjectDeclarationArray *registry) {
    auto *declaration = CreateCKObjectDeclaration("Nop");
    declaration->SetGuid(NopGuid);
    declaration->SetType(CKDLL_BEHAVIORPROTOTYPE);
    declaration->SetCompatibleClassId(CKCID_BEOBJECT);
    declaration->SetCreationFunction(Prototype);
    CKStoreDeclaration(registry, declaration);
}
int Count() { return 1; }
CKPluginInfo *Info(int) {
    static CKPluginInfo info{};
    info.m_Type = CKPLUGIN_BEHAVIOR_DLL;
    info.m_GUID = CKGUID(0x6e7a4381, 0x2d056b95);
    info.m_Summary = "Legacy ScriptHelper regression fixture";
    info.m_Description = info.m_Summary;
    return &info;
}
bool Live(CKContext *context, CKObject *object) {
    // Never dereference an untrusted cached pointer to validate it.
    for (int id = 1; id <= context->GetObjectCount(); ++id)
        if (context->GetObject(id) == object) return object != nullptr;
    return false;
}
CKBehavior *Block(CKContext *context) {
    auto *block = static_cast<CKBehavior *>(context->CreateObject(CKCID_BEHAVIOR));
    Require(block && block->InitFromGuid(NopGuid) == CK_OK, "create Nop block");
    return block;
}
void CheckWorld(CKContext *context) {
    auto *graph = static_cast<CKBehavior *>(context->CreateObject(CKCID_BEHAVIOR));
    graph->UseGraph();
    auto *source = Block(context), *destination = Block(context);
    Require(graph->AddSubBehavior(source) == CK_OK, "attach source");
    Require(graph->AddSubBehavior(destination) == CK_OK, "attach destination");
    auto makeLink = [&]() {
        auto *link = static_cast<CKBehaviorLink *>(context->CreateObject(CKCID_BEHAVIORLINK));
        Require(link->SetInBehaviorIO(source->GetOutput(0)) == CK_OK, "set source IO");
        Require(link->SetOutBehaviorIO(destination->GetInput(0)) == CK_OK, "set destination IO");
        Require(graph->AddSubBehaviorLink(link) == CK_OK, "attach link");
        return link;
    };
    auto *first = makeLink(), *second = makeLink();
    const int before = context->GetObjectCount();
    ScriptHelper::DeleteLink(graph, first);
    Require(Live(context, first->GetInBehaviorIO()) && Live(context, first->GetOutBehaviorIO()),
            "DeleteLink returned an endpoint outside the live context");
    auto *sink = first->GetInBehaviorIO()->GetOwner();
    Require(Live(context, sink) && sink != source && sink != destination, "sink is not a live separate behavior");
    Require(first->GetOutBehaviorIO()->GetOwner() == sink, "sink endpoints disagree");
    Require(context->GetObjectCount() == before + 3, "first DeleteLink must allocate exactly one Nop and its two IOs");
    ScriptHelper::DeleteLink(graph, second);
    Require(second->GetInBehaviorIO() == first->GetInBehaviorIO() &&
            second->GetOutBehaviorIO() == first->GetOutBehaviorIO(), "same world did not reuse its sink");
    Require(context->GetObjectCount() == before + 3, "second DeleteLink allocated another sink");
    Require(graph->GetSubBehaviorLinkCount() == 2, "legacy DeleteLink removed a graph link");
}
}
int main(int argc, char **argv) {
    try {
        Require(argc == 2, "expected contexts or reload");
        Require(CKStartUp() == CK_OK, "CKStartUp");
        Require(CKGetPluginManager()->RegisterStaticPlugin("LegacyLinkRegression", Count, Info, nullptr, Register) == CK_OK, "register Nop");
        CKContext *a = nullptr, *b = nullptr;
        Require(CKCreateContext(&a, nullptr, 0, 0) == CK_OK, "create first context");
        CheckWorld(a);
        if (std::string_view(argv[1]) == "contexts") {
            Require(CKCreateContext(&b, nullptr, 0, 0) == CK_OK, "create second context");
            CheckWorld(b);
            CKCloseContext(b);
        } else {
            Require(std::string_view(argv[1]) == "reload", "unknown regression case");
            for (int i = 0; i < 12; ++i) {
                a->ClearAll();
                CheckWorld(a);
            }
        }
        CKCloseContext(a);
        CKShutdown();
        std::printf("PASS %s\n", argv[1]);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
