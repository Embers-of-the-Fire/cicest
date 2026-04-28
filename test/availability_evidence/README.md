# Availability Evidence Suite

This suite checks paper-facing availability evidence through stable substring
patterns rather than full TyIR golden files. Each `.patterns` file declares the
source program with `# source:`, the rule shape with `# rule:`, and then ordered
substrings that must appear in the produced output.

TyIR specs run:

```text
cstc_inspect <source> --out-type tyir
```

Diagnostic specs compile the source and require compilation to fail, then check
ordered substrings in compiler stderr.
