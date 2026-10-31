# Legacy ScriptHelper lifecycle regression

This standalone client uses the real CK2 and public `ScriptHelper::DeleteLink` API.
It registers a minimal NOP prototype and checks:

- Each context receives its own live link sink.
- Repeated calls in one world reuse one NOP (three CK object slots).
- Links remain in their graph, preserving the legacy redirect semantics.
- Twelve `ClearAll`/reconstruction cycles never reuse a freed endpoint.

Configure with `CK2_SOURCE_DIR`, `VXMATH_SOURCE_DIR`, `BML_LIBRARY`, `CK2_LIBRARY`
and `VXMATH_LIBRARY` pointing to the corresponding built dependencies, then run
`cmake --build <build-dir>` and `ctest --test-dir <build-dir> --output-on-failure`.
On Windows, the dependency DLL directories must also be on `PATH`.

The same executable can test an older BML library through the platform's dynamic
library search path. The old process-static NOP cache fails the contexts test;
with an instrumented CK2 and ASan runtime, the reload test reports a use-after-free
in `CKBehaviorLink::SetInBehaviorIO`. Verify the loaded library paths when using
ASan: injecting its runtime alone does not instrument an ordinary CK2 build.

These tests cover the legacy link sink's lifetime, not TAS trajectory fidelity or
gameplay snapshot restoration.
