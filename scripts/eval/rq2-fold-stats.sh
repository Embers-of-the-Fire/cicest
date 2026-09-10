#!/usr/bin/env bash
# RQ2 evidence harness: per-program CT-folded / residual / total TyIR node
# counts across the full e2e suite, emitted as one machine-readable CSV table.
#
# Usage: rq2-fold-stats.sh <cstc_inspect-binary> <e2e-suite-dir> <output.csv>
#
# Columns: program,suite,status,folded_nodes,residual_calls,total_nodes
# - suite:  pass | fail_runtime | fail_compile (e2e suite directory)
# - status: ok (lowered and folded) | fail_compile (no TyIR produced; counts
#   left empty — the program is rejected by design)
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <cstc_inspect-binary> <e2e-suite-dir> <output.csv>" >&2
    exit 2
fi

inspect_bin="$1"
e2e_dir="$2"
output="$3"

mkdir -p "$(dirname "$output")"
echo "program,suite,status,folded_nodes,residual_calls,total_nodes" > "$output"

for suite in pass fail_runtime fail_compile; do
    suite_dir="${e2e_dir}/${suite}"
    [[ -d "${suite_dir}" ]] || continue
    while IFS= read -r source; do
        rel="${source#"${e2e_dir}"/}"
        if row="$("${inspect_bin}" "${source}" --out-type stats 2>/dev/null | tail -n 1)"; then
            # The inspector echoes the input path as the first CSV field;
            # replace it with the suite-relative path.
            echo "${rel},${suite},ok,${row#*,}" >> "${output}"
        else
            echo "${rel},${suite},fail_compile,,," >> "${output}"
        fi
    done < <(find "${suite_dir}" -name '*.cst' | LC_ALL=C sort)
done
