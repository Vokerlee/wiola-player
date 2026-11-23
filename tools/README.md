# Repository tools

This tree contains maintained development-tool implementations. It does not participate in the player runtime. Each independent tool abstraction owns its implementation and tests below its own folder.

The current architecture tools inventory tracked-file ownership. Run their checks from the checkout with `python3 -m tools.architecture.commands.check_ownership`; run their tests with `python3 -m unittest discover -s tools -p 'test_*.py'`.

```mermaid
flowchart TD
  Tools[Repository tools] --> Architecture[Architecture checks]
  Architecture --> Inventory[Tracked-file inventory]
  Architecture --> Registry[Ownership registry]
```

[Architecture](architecture/README.md) describes the actual components. Tool paths and policies are explicit inputs or package resources; there are no assumptions about a developer home directory or build output location.
