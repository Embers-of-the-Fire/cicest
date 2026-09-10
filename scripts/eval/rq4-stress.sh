#!/usr/bin/env bash
# RQ4 stress evidence harness: scaling and worst-case behavior of the fused
# availability checking + const-eval folding phase (lower_fold) on synthetic
# programs far beyond suite complexity.
#
# Compiles every scenario program (normal_*/worst_*) <runs> times (default 3)
# with `cstc --time-phases` and reports per-phase medians, together with the
# fold/residual statistics from `cstc_inspect --out-type stats`. The
# budget_trip.cst program is compiled once and MUST fail; its first
# diagnostic line is recorded as evidence that pathological folds are capped
# by the evaluator budgets rather than silently consuming compile time.
#
# Usage: rq4-stress.sh <cstc-binary> <cstc_inspect-binary> <stress-dir> <output.csv> [runs]
#
# Columns: program,scenario,size,folded_nodes,residual_calls,total_nodes,
#          parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,link_ms,total_ms
# Side output: <output.csv>.budget-diagnostic.txt
set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
    echo "usage: $0 <cstc-binary> <cstc_inspect-binary> <stress-dir> <output.csv> [runs]" >&2
    exit 2
fi

cstc_bin="$1"
inspect_bin="$2"
stress_dir="$3"
output="$4"
runs="${5:-3}"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

median() {
    # Median of newline-separated numbers on stdin.
    LC_ALL=C sort -n | awk '{a[NR]=$1} END {print (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2}'
}

mkdir -p "$(dirname "$output")"
echo "program,scenario,size,folded_nodes,residual_calls,total_nodes,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,link_ms,total_ms" > "$output"

for source in "${stress_dir}"/normal_*.cst "${stress_dir}"/worst_*.cst; do
    base="$(basename "${source}" .cst)"
    scenario="${base%%_*}"
    size="${base##*_}"

    # Fold/residual statistics (single run; deterministic). The stats output
    # is a two-line CSV with a header row; take the data row.
    stats="$("${inspect_bin}" "${source}" --out-type stats | tail -n 1)"
    folded="$(echo "${stats}" | cut -d, -f2)"
    residual="$(echo "${stats}" | cut -d, -f3)"
    nodes="$(echo "${stats}" | cut -d, -f4)"

    rm -f "${tmpdir}"/phase_*
    for _ in $(seq "${runs}"); do
        "${cstc_bin}" "${source}" -o "${tmpdir}/artifact" --emit exe --time-phases \
            > /dev/null 2> "${tmpdir}/phases.csv"
        while IFS=, read -r _phase name ms; do
            [[ "${name}" == "" ]] && continue
            echo "${ms}" >> "${tmpdir}/phase_${name}"
        done < "${tmpdir}/phases.csv"
    done
    row="${base},${scenario},${size},${folded},${residual},${nodes}"
    for phase in parse_modules lower_fold lir codegen link total; do
        row="${row},$(median < "${tmpdir}/phase_${phase}")"
    done
    echo "${row}" >> "${output}"
done

# The budget-trip program must fail; record the diagnostic as evidence.
if "${cstc_bin}" "${stress_dir}/budget_trip.cst" -o "${tmpdir}/artifact" --emit exe \
        > /dev/null 2> "${tmpdir}/budget_err.txt"; then
    echo "error: budget_trip.cst compiled successfully; expected a budget diagnostic" >&2
    exit 1
fi
head -n 1 "${tmpdir}/budget_err.txt" > "${output}.budget-diagnostic.txt"
echo "budget-trip diagnostic: $(cat "${output}.budget-diagnostic.txt")"
