# Agent Context

## Required Workflow

1. **Run tests via Nix**
   - For test execution, run:
   - `nix run .#tests`
   - Do not manually compile and run tests outside this command unless explicitly requested.

2. **Run lint via Nix**
   - All code changes must be linted with:
   - `nix run .#lint`

3. **Show Conventional Commit messages**
   - For every change the agent makes, present a corresponding Conventional Commit message suggestion to the user.
   - Use standard Conventional Commit types (for example: `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`).

4. **Document new packages thoroughly**
   - Every newly added package must include its own dedicated `README.md`.
   - All package code must be well and fully documented, including:
     - Exposed/public APIs
     - Internal implementation details

5. **Test all features comprehensively**
   - Every feature must include tests.
   - Add as many meaningful tests as possible.
   - Target ideal test coverage of 100% whenever practical.
   - At minimum, tests must cover:
     - Hot paths (most-used flows)
     - Edge cases

## Priority

If multiple instruction sources exist, follow direct user instructions first, then this file, unless another higher-priority policy explicitly overrides it.
