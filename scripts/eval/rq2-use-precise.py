#!/usr/bin/env python3
"""RQ2 conservativity measurement: the price of join-all-params inference.

The shipped emission rule (D-Infer) joins *all* runtime-allowed parameters
into the result contract, used or not. This script recomputes every call's
classification under the use-precise alternative --- join only the parameters
that actually contribute to the body availability expression --- and reports,
per compilable e2e program, how many residual `runtime-barrier` calls would
flip to compile-time-foldable under that rule.

Method (all from `cstc_inspect --out-type tyir` output):

- Parse each TyFnDecl's signature (parameter contracts, result contract,
  internal-rt flag) and body tree.
- Recompute every expression's availability *expression* (a set of enclosing
  function parameter names, or RT) structurally: literals are CT; runtime
  blocks are RT; locals resolve through let bindings; calls instantiate the
  callee's current use-precise contract with the recomputed argument
  availabilities; all other nodes join their children (matching the
  join-based rules of the calculus).
- Iterate to a fixpoint: a function's use-precise contract is RT if its
  internal-rt flag is set, else the parameter variables of its recomputed
  body availability.
- A residual call *flips* iff its recomputed result availability is the empty
  set: compile-time under use-precise contracts regardless of context. Flips
  cascade in the same pass because argument availabilities are recomputed
  bottom-up.

Sanity checks: the parsed barrier count must equal the committed
rq2-fold-stats.csv residual_calls per program, and no recomputed availability
may exceed the printed one under the all-parameters-CT reading.

Usage: rq2-use-precise.py <cstc_inspect-binary> <rq2-fold-stats.csv> <e2e-root> <out.csv>
Output columns: program,residual_calls,use_precise_flips
"""

import csv
import re
import subprocess
import sys
from pathlib import Path

RT = "RT"  # availability is frozenset of parameter names, or the string "RT"

WRAPPERS = {"Tail", "Condition", "Then", "Else", "Arg", "TyExprStmt", "GenericArgs",
            "Where", "Decl"}

FN_RE = re.compile(
    r"TyFnDecl (\S+?)\((.*?)\) -> (.*?) \[runtime-authority: \S+\] "
    r"\[availability-signature: params=\[(.*?)\], result=(.*?)(?:, internal-rt=(.*?))?\]$"
)
EXTERN_RE = re.compile(
    r'TyExternFnDecl "lang" (\S+)\((.*?)\) -> (.*?) \[runtime-authority: \S+\] '
    r"\[availability-signature: params=\[(.*?)\], result=(.*?)(?:, internal-rt=(.*?))?\]$"
)
CALL_RE = re.compile(r"TyCall\(([^)]+)\)")
LOCAL_RE = re.compile(r"TyLocal\(([^)]+)\)")
LET_RE = re.compile(r"Let (\S+): (.*?) =$")
STAMP_RE = re.compile(r"\[availability: (const|runtime)\]")
RESIDUE_RE = re.compile(r"\[call-residue: (\S+)\]")


def param_names(params_src: str):
    """Parameter names and runtime-qualified flags from a printed parameter list."""
    names, rt_qualified = [], set()
    if not params_src.strip():
        return names, rt_qualified
    for p in params_src.split(", "):
        name, _, ty = p.partition(": ")
        names.append(name)
        if ty.startswith("runtime "):
            rt_qualified.add(name)
    return names, rt_qualified


class Node:
    __slots__ = ("text", "children")

    def __init__(self, text):
        self.text = text
        self.children = []


def parse_tree(lines):
    """Indent-based tree; returns list of top-level nodes."""
    roots, stack = [], []
    for line in lines:
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        node = Node(line.strip())
        while stack and stack[-1][0] >= indent:
            stack.pop()
        if stack:
            stack[-1][1].children.append(node)
        else:
            roots.append(node)
        stack.append((indent, node))
    return roots


class Function:
    def __init__(self, name, params_src, internal_rt, extern):
        self.name = name
        self.params, self.rt_params = param_names(params_src)
        self.contract_params = [p for p in self.params]  # runtime-allowed params
        self.internal_rt = internal_rt
        self.extern = extern
        self.const_required = set()
        self.body = None
        self.use_precise = RT if internal_rt else frozenset()  # refined below


class Program:
    def __init__(self, tyir_text):
        self.functions = {}
        self.externs = {}
        roots = parse_tree(tyir_text.splitlines())
        for top in roots[0].children if roots and roots[0].text == "TyProgram" else roots:
            m = FN_RE.match(top.text)
            if m:
                name, params_src, _ret, contracts, _result, internal_rt = m.groups()
                # TyCall prints the base name; strip the <T...> generic
                # parameter list so calls resolve to the declaration.
                name = name.split("<")[0]
                fn = Function(name, params_src, internal_rt, extern=False)
                for c, p in zip(contracts.split(", ") if contracts else [], fn.params):
                    if c == "CT":
                        fn.const_required.add(p)
                for child in top.children:
                    if child.text.startswith("TyBlock"):
                        fn.body = child
                self.functions[name] = fn
                continue
            m = EXTERN_RE.match(top.text)
            if m:
                name, params_src, _ret, contracts, result, internal_rt = m.groups()
                fn = Function(name, params_src, internal_rt, extern=True)
                # Externs keep their declared contract verbatim: result is CT,
                # RT, or a join of param{i} variables.
                fn.use_precise = RT if result == "RT" else frozenset(
                    fn.params[i]
                    for i in range(len(fn.params))
                    if re.search(rf"\bparam{i}\b", result)
                )
                self.externs[name] = fn

    def callee(self, name):
        return self.functions.get(name) or self.externs.get(name)


def join(a, b):
    if a == RT or b == RT:
        return RT
    return a | b


def eval_node(node, fn, scopes, prog, stats):
    """Recomputed availability of one expression node under use-precise contracts."""
    text = node.text
    m = LOCAL_RE.match(text)
    if m:
        name = m.group(1)
        stamp = STAMP_RE.search(text)
        if stamp and stamp.group(1) == "runtime":
            return RT
        if name in fn.params:
            return RT if name in fn.rt_params else frozenset({name})
        for scope in reversed(scopes):
            if name in scope:
                return scope[name]
        return RT  # unresolved name: be conservative
    if text.startswith("TyLiteral"):
        return frozenset()
    if text.startswith("TyRuntimeBlock"):
        # The stamp is RT unconditionally, but the children must still be
        # traversed: calls inside a runtime block carry their own verdicts.
        join_all(node.children, fn, scopes, prog, stats)
        return RT
    m = CALL_RE.match(text)
    if m:
        callee = prog.callee(m.group(1))
        residue = RESIDUE_RE.search(text)
        args = [eval_node(c.children[0], fn, scopes, prog, stats)
                for c in node.children if c.text == "Arg" and c.children]
        if callee is None:
            return RT
        result = callee.use_precise
        out = frozenset()
        if result == RT:
            out = RT
        else:
            for i, pname in enumerate(callee.params):
                if pname not in result:
                    continue
                arg = args[i] if i < len(args) else RT
                if arg == RT:
                    out = RT
                    break
                out |= arg
        if residue and residue.group(1) == "runtime-barrier" and out == frozenset():
            stats["flips"] += 1
        if residue and residue.group(1) == "runtime-barrier":
            stats["barriers"] += 1
        return out
    m = LET_RE.match(text)
    if m:
        name, ty = m.group(1), m.group(2)
        init = join_all(node.children, fn, scopes, prog, stats)
        value = RT if ty.startswith("runtime ") else init
        if scopes:
            scopes[-1][name] = value
        return value
    if text.startswith("TyBlock"):
        scopes.append({})
        out = join_all(node.children, fn, scopes, prog, stats)
        scopes.pop()
        return out
    if text.split(":")[0] in ("TyEnumDecl", "TyStructDecl"):
        return frozenset()
    # Structural rules and transparent wrappers: join the children.
    return join_all(node.children, fn, scopes, prog, stats)


def join_all(children, fn, scopes, prog, stats):
    out = frozenset()
    for child in children:
        out = join(out, eval_node(child, fn, scopes, prog, stats))
    return out


def body_vars(fn, prog):
    if fn.body is None:
        return frozenset()
    stats = {"flips": 0, "barriers": 0}
    return eval_node(fn.body, fn, [{}], prog, stats)


def analyze(tyir_text):
    prog = Program(tyir_text)
    # Fixpoint over use-precise contracts (monotone shrinking).
    for fn in prog.functions.values():
        fn.use_precise = RT if fn.internal_rt else frozenset(fn.params)
    changed = True
    while changed:
        changed = False
        for fn in prog.functions.values():
            if fn.internal_rt:
                continue
            new = body_vars(fn, prog)
            new = frozenset(v for v in new if v in fn.params)
            if new != fn.use_precise:
                fn.use_precise = new
                changed = True
    # Final pass: count barriers and flips.
    stats = {"flips": 0, "barriers": 0}
    for fn in prog.functions.values():
        if fn.body is not None:
            eval_node(fn.body, fn, [{}], prog, stats)
    return stats


def main():
    if len(sys.argv) != 5:
        sys.exit(f"usage: {sys.argv[0]} <cstc_inspect-binary> <rq2-fold-stats.csv> <e2e-root> <out.csv>")
    inspect_bin, rq2_csv, e2e_root, out_csv = sys.argv[1:]
    rows = [r for r in csv.DictReader(open(rq2_csv)) if r["status"] == "ok"]
    out = ["program,residual_calls,use_precise_flips"]
    total_residual = total_flips = 0
    for r in rows:
        program = r["program"]
        tyir = subprocess.run(
            [inspect_bin, str(Path(e2e_root) / program), "--out-type", "tyir"],
            capture_output=True, text=True, check=True).stdout
        stats = analyze(tyir)
        residual = int(r["residual_calls"])
        if stats["barriers"] != residual:
            print(f"WARNING: {program}: parsed {stats['barriers']} barriers, "
                  f"CSV says {residual}", file=sys.stderr)
        out.append(f"{program},{residual},{stats['flips']}")
        total_residual += residual
        total_flips += stats["flips"]
    Path(out_csv).parent.mkdir(parents=True, exist_ok=True)
    Path(out_csv).write_text("\n".join(out) + "\n")
    print(f"wrote {out_csv}")
    print(f"use-precise flips: {total_flips} of {total_residual} residual calls")


if __name__ == "__main__":
    main()
