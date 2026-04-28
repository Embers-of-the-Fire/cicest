# Availability Coverage Inventory

This inventory maps availability rule shapes used in the paper to checked
artifact evidence. `availability_evidence` entries are pattern specs run by the
new inspection/diagnostic CTest target. `e2e` entries are existing compile/run
tests that continue to run through the `e2e` CTest target.

| Rule shape | Evidence | Notes |
| --- | --- | --- |
| Call-site lifting | `availability_evidence/tyir/availability_validation.patterns`; `test/e2e/pass/runtime/plain_call_lifting.cst`; `test/e2e/pass/runtime/plain_generic_call_lifting.cst` | Checks folded static calls and runtime-qualified residual calls. |
| CT-required acceptance | `availability_evidence/tyir/ct_required_parameters.patterns`; `availability_evidence/tyir/availability_window_config.patterns` | Checks `!runtime` parameter display and folded CT calls. |
| CT-required rejection | `availability_evidence/diagnostics/ct_required_runtime_result.patterns`; `test/e2e/fail_compile/generics/ct_required_generic_runtime_argument.cst` | Checks parameter-specific diagnostics with expected/found availability shapes. |
| Runtime-result functions | `test/e2e/pass/runtime/runtime_result_function.cst` | Ensures explicit runtime result annotations remain accepted. |
| Runtime block partial normalization | `availability_evidence/tyir/runtime_block_partial_normalization.patterns` | Checks a pure expression folded under an explicit `TyRuntimeBlock`. |
| Runtime block dynamic calls | `test/e2e/pass/runtime/runtime_block_dynamic_call.cst`; `availability_evidence/tyir/availability_validation.patterns` | Checks runtime blocks with residual dynamic calls and independently folded arguments. |
| Structural joins | `availability_evidence/tyir/runtime_join_promotion.patterns` | Checks a runtime-qualified `TyIf` with static and runtime branches. |
| Rejected reverse demotion | `availability_evidence/diagnostics/runtime_join_demotion.patterns`; `test/e2e/fail_compile/type_errors/plain_call_runtime_return_demotion.cst` | Checks runtime-qualified values cannot flow into compile-time result positions. |
| Compile-time assertion failure | `test/e2e/fail_compile/const_eval/availability_validation_assert.cst`; `test/e2e/fail_compile/const_eval/assert_false.cst` | Keeps compile-time assertion failures covered by negative e2e tests. |
| Runtime assertion residualization | `test/e2e/pass/runtime/availability_validation.cst`; `test/e2e/pass/runtime/availability_window_config.cst` | Runtime-qualified calls through asserting helpers compile and remain runtime-residualized. |
| Aggregate/generic lifting | `availability_evidence/tyir/availability_window_config.patterns`; `test/e2e/pass/runtime/availability_generic_config.cst` | Checks lifted generic constructors/helpers and aggregate runtime qualification. |
