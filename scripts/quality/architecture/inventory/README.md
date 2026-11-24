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

The same adapter resolves trusted revisions and reads their committed policy blobs without changing the index. Working-tree reads retain repository-relative identity from nested queries and reject paths escaping the checkout. Deleted paths and the actual commit review message support acceptance without a second Git implementation.

`read_previous(path, reference)` preserves trusted policy comparisons through committed directory reorganizations. It first reads the exact path at the verified revision. If absent, only Git's committed rename evidence from that revision to HEAD can identify its earlier path. The returned bytes always come from the trusted revision, never the current file. New, deleted or unmatched policies fail; similarity matching is not permission to reset a baseline. Invalid revisions/paths and Git failures remain errors. This operation never stages, checks out or modifies repository content.

The `root` property asks Git for the actual owning worktree, including nested callers. Consumers resolve package-owned resources relative to that root; they do not infer checkout depth. Worktree reads reject symlinks escaping that root.
