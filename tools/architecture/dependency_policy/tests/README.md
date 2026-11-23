# Tests

Policy tests inject independent forbidden edges; passing the production inventory alone would not exercise rejection.

Related-part cases distinguish internal private access from an external private import, and reject overlapping/unassigned parts and cross-capability fusion. Source/destination membership failures remain explicit diagnostics.

```mermaid
flowchart LR
  Input[Independent input] --> Check[Owning dependency check]
  Check --> Result[Accepted contract or diagnostic]
```
