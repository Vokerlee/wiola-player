# Formatting and Git hook

This component owns tracked input selection, the two formatter protocols and installation of one thin pre-commit hook. Import `format_sources` and `install_hook` from `scripts.formatting`; implementation modules stay private.

```mermaid
flowchart LR
  Caller[Developer, CI or hook] --> CLI[One formatting command]
  CLI --> Git[Actual root and tracked inputs]
  Git --> Rules[One immutable formatter specification]
  Rules --> Tools[Pinned clang-format and gersemi]
```

Run `python3 -m scripts.formatting format cpp|cmake|all [--check] [--files PATH...]`. The default selects existing tracked inputs; explicit files must be tracked, exist, match the selected formatter and remain within the actual worktree. File arguments are literal absolute paths, preserving spaces and preventing option interpretation. Missing tracked files are skipped only during default inventory selection. No current-directory mutation or guessed checkout depth is used.

Root `.clang-format` and `.gersemirc` remain conventional configuration entrypoints; formatter versions remain pinned in requirements.txt. Their styles are not repeated here. Subprocess output and nonzero/missing-tool failures propagate. Without --check the selected files are rewritten; explicit selection does not rewrite unrelated files.

Run `python3 -m scripts.formatting install-hooks`. Git supplies the actual hooks directory, including worktrees and core.hooksPath. Installation atomically replaces a symlink, including a broken old launcher link, while refusing a regular user hook file and preserving any old symlink referent. The hook delegates to this same canonical check and never silently skips missing tools. It checks working-tree formatting; it does not claim to validate separately staged partial-file contents. No root formatting launcher is retained.
