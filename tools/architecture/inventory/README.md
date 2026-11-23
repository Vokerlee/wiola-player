# Git inventory

`GitInventory` obtains the authoritative tracked-file set from a supplied working-tree location. It includes staged additions and index-owned unstaged deletions, excludes untracked working plans/build products, and returns repository-relative paths even when called from a nested directory.

```mermaid
flowchart LR
  Location[Working-tree location] --> Adapter[GitInventory]
  Adapter --> Git[git ls-files: root pathspec, NUL output]
  Git --> Paths[Tracked UTF-8 relative paths]
```

The adapter never mutates the index or discovers ownership. Git failure and bounded process failure remain distinguishable subprocess exceptions for the command boundary to report. NUL-separated output preserves spaces and Unicode without shell parsing.

[Test contracts](tests/README.md) use a real isolated Git index and a nonrepository failure case. Replacing the source of tracked paths does not require changing the ownership registry.
