#!/usr/bin/env bash
# RQ4 evidence harness: compile-phase timing across the e2e suite.
#
# Measures the availability checking+folding phase (lower_fold) as a fraction of
# total compile time for every compilable e2e program, using `cstc
# --time-phases` (CSV rows on stderr).
#
# Usage: rq4-compile-phases.sh <cstc-binary> <e2e-suite-dir> <output.csv> [runs]
#
# Columns: program,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,link_ms,total_ms
# Each program is compiled <runs> times (default 3) and the median per phase is
# reported. fail_compile programs are skipped: they are rejected by design and
# do not reach the back end.
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "usage: $0 <cstc-binary> <e2e-suite-dir> <output.csv> [runs]" >&2
    exit 2
fi

cstc_bin="$1"
e2e_dir="$2"
output="$3"
runs="${4:-3}"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

median() {
    # Median of newline-separated numbers on stdin.
    LC_ALL=C sort -n | awk '{a[NR]=$1} END {print (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2}'
}

mkdir -p "$(dirname "$output")"
echo "program,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,link_ms,total_ms" > "$output"

while IFS= read -r source; do
    rel="${source#"${e2e_dir}"/}"
    # Accumulate per-phase samples across runs in per-phase files.
    rm -f "${tmpdir}"/phase_*
    for _ in $(seq "${runs}"); do
        "${cstc_bin}" "${source}" -o "${tmpdir}/artifact" --emit exe --time-phases \
            > /dev/null 2> "${tmpdir}/phases.csv" || true
        while IFS=, read -r _phase name ms; do
            [[ "${name}" == "" ]] && continue
            echo "${ms}" >> "${tmpdir}/phase_${name}"
        done < "${tmpdir}/phases.csv"
    done
    row="${rel}"
    for phase in parse_modules lower_fold lir codegen link total; do
        if [[ -f "${tmpdir}/phase_${phase}" ]]; then
            row="${row},$(median < "${tmpdir}/phase_${phase}")"
        else
            row="${row},"
        fi
    done
    echo "${row}" >> "${output}"
done < <(find "${e2e_dir}/pass" "${e2e_dir}/fail_runtime" -name '*.cst' | LC_ALL=C sort)
