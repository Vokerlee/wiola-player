# Data

`modules.json` explicitly assigns current owners, dependency directions and Qt capability. `legacy.json` identifies exact current violations and their removal responsibility. Remove resolved entries; do not regenerate the baseline to accept a new violation.

```mermaid
flowchart LR
  Input[Independent input] --> Check[Owning dependency check]
  Check --> Result[Accepted contract or diagnostic]
```
