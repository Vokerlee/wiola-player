# Build environments

This grouping contains adapters for preparing external build tools. Each child owns its package manager interaction, reuse contract and tests. Player targets and GUI choices remain with their callers.

```mermaid
flowchart LR
  Caller[Local build or CI provider] --> Ubuntu[ubuntu_packages]
  Ubuntu --> APT[Ubuntu APT]
```
