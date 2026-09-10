# Evaluation harnesses (working4 RQ2/RQ3/RQ4)

Scripts that turn the compiler's instrumentation into machine-readable evidence
for the paper. All scripts take explicit binary/input/output paths; run them
after `nix run .#build-tests` so `./build/compiler/...` binaries exist.

## RQ2 — folded/residual instrumentation

```bash
bash scripts/eval/rq2-fold-stats.sh ./build/compiler/cstc_inspect/cstc_inspect test/e2e <out.csv>
```

Runs `cstc_inspect --out-type stats` over every e2e program and emits one CSV
(`program,suite,status,folded_nodes,residual_calls,total_nodes`). Programs in
`fail_compile` are rejected by design; their rows carry `status=fail_compile`
with empty counts.

## RQ3 — signature stability / caching baseline

```bash
bash scripts/eval/rq3-signature-stability.sh ./build/compiler/cstc/cstc ./build/compiler/cstc_inspect/cstc_inspect <out.csv>
```

Builds a two-module program, changes a library callee's *body* without touching
its signature, and rebuilds. The CSV records the callee's printed
availability-signature for both builds (byte-identical) plus per-phase compile
times. The current pipeline performs no cross-invocation caching, so the client
re-check/re-fold work is measured, and reported, in full on both builds.

## RQ4 — compile-phase timing

```bash
bash scripts/eval/rq4-compile-phases.sh ./build/compiler/cstc/cstc test/e2e <out.csv> [runs]
```

Compiles every compilable e2e program `<runs>` times (default 3) with
`cstc --time-phases` and reports per-phase medians as
`program,parse_modules_ms,lower_fold_ms,lir_ms,codegen_ms,link_ms,total_ms`.
`lower_fold` is the availability checking + const-eval folding phase whose share
of total compile time the RQ4 figure reports.

```bash
python3 scripts/eval/rq4-plot.py <rq4.csv> <out.png>
```

Renders the RQ4 figure from the CSV (requires matplotlib; e.g.
`nix-shell -p 'python3.withPackages (ps: [ps.matplotlib])' --run '...'`).
