#!/usr/bin/env bash
# RQ3 evidence harness: signature stability and client re-check work across a
# callee body change.
#
# Builds a two-module program (client imports helpers from a library), then
# changes the library callee's *body* without touching its signature, and
# rebuilds. For each build it records:
#   - the callee's printed availability-signature (extracted from inspected
#     TyIR), proving the client's contract view is byte-identical;
#   - per-phase compile times (cstc --time-phases), showing exactly what work
#     the current (non-incremental) pipeline redoes for the client.
#
# The compiler currently performs no cross-invocation caching: the CSV reports
# the measured full re-check/re-fold work on both builds, so the "work avoided"
# column is the honest zero of the current pipeline.
#
# Usage: rq3-signature-stability.sh <cstc-binary> <cstc_inspect-binary> <output.csv>
#
# Columns: build,callee_signature,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,total_ms
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <cstc-binary> <cstc_inspect-binary> <output.csv>" >&2
    exit 2
fi

cstc_bin="$1"
inspect_bin="$2"
output="$3"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

cat > "${tmpdir}/window_lib.cst" <<'EOF'
pub fn checked_add(a: num, b: num, limit: const num) -> num {
    assert(a + b <= limit);
    a + b
}
EOF

cat > "${tmpdir}/client.cst" <<'EOF'
import { checked_add } from "window_lib.cst";

fn main() {
    let total: num = checked_add(20, 2, 100);
    assert_eq(total, 22);
}
EOF

build_and_measure() {
    local label="$1"
    local phases
    "${cstc_bin}" "${tmpdir}/client.cst" -o "${tmpdir}/client_${label}" --emit exe \
        --time-phases > /dev/null 2> "${tmpdir}/phases_${label}.csv"

    local signature
    # Strip everything up to the signature field and the closing bracket, then
    # quote the value (it contains commas).
    signature="$("${inspect_bin}" "${tmpdir}/client.cst" --out-type tyir \
        | grep 'TyFnDecl __cst_mod_[0-9]*__checked_add' \
        | sed 's/^.*availability-signature: /availability-signature: /; s/\]$//')"

    local parse= lower_fold= lir= codegen= total=
    while IFS=, read -r _phase name ms; do
        case "${name}" in
        parse_modules) parse="${ms}" ;;
        lower_fold) lower_fold="${ms}" ;;
        lir) lir="${ms}" ;;
        codegen) codegen="${ms}" ;;
        total) total="${ms}" ;;
        esac
    done < "${tmpdir}/phases_${label}.csv"
    echo "${label},\"${signature}\",${parse},${lower_fold},${lir},${codegen},${total}" >> "${output}"
}

mkdir -p "$(dirname "$output")"
echo "build,callee_signature,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,total_ms" \
    > "${output}"

build_and_measure before_body_change

# Change ONLY the callee body: the signature (parameter list, const
# requirement, result contract) is untouched.
cat > "${tmpdir}/window_lib.cst" <<'EOF'
pub fn checked_add(a: num, b: num, limit: const num) -> num {
    let sum: num = a + b;
    assert(sum <= limit);
    sum
}
EOF

build_and_measure after_body_change
