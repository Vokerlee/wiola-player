"""Reviewed current contracts for the architecture tooling components."""

from tools.architecture.contracts.catalog import AbstractionContract


CONTRACTS = (
    AbstractionContract(
        "tooling.package", "tools", "Repository-tool package and contributor entrypoint",
        ("Player runtime does not depend on development tools",), grouping=True,
    ),
    AbstractionContract(
        "architecture.package", "tools/architecture", "Groups independent architecture checks",
        ("Current ownership is not a claim that legacy architecture is clean",), grouping=True,
    ),
    AbstractionContract(
        "architecture.command_group", "tools/architecture/commands", "Groups command entrypoints",
        ("Entrypoints assemble components rather than contain their implementations",), grouping=True,
    ),
    AbstractionContract(
        "architecture.ownership", "tools/architecture/ownership", "One-owner tracked-file registry",
        ("Every path has one explicit owner", "Paths and owner identities are canonical",
         "Returned ownership values are immutable"),
        ("tools/architecture/ownership/registry.py",),
    ),
    AbstractionContract(
        "architecture.git_inventory", "tools/architecture/inventory", "Reads tracked Git paths",
        ("Untracked plans are not product files", "The index is never modified",
         "Nested queries retain repository-relative identity"),
        ("tools/architecture/inventory/git_inventory.py",),
    ),
    AbstractionContract(
        "architecture.cmake_inventory", "tools/architecture/cmake_inventory", "Reads configured target metadata",
        ("Declared targets are not proof of executed tests", "No preset-to-path guessing",
         "External and generated sources are distinguished"),
        ("tools/architecture/cmake_inventory/inventory.py",),
    ),
    AbstractionContract(
        "architecture.check_ownership", "tools/architecture/commands/check_ownership", "Runs inventory reconciliation",
        ("Checking never configures or builds the player", "Failure exits remain distinguishable"),
        ("tools/architecture/commands/check_ownership/__main__.py",),
    ),
    AbstractionContract(
        "architecture.contracts", "tools/architecture/contracts", "Checks abstraction cohesion and explicit migration debt",
        ("A reviewed implementation has one owning subtree", "Legacy allowances cannot grow implicitly",
         "Public declarations and READMEs belong to their abstraction"),
        ("tools/architecture/contracts/catalog.py",),
    ),
    AbstractionContract(
        "architecture.dependency_graph", "tools/architecture/dependency_graph", "Observes explicit source and configured prerequisites",
        ("Source text is never executed", "Configured prerequisites are not claimed as direct links",
         "Observed edges carry source and destination owners"),
        ("tools/architecture/dependency_graph/graph.py",),
    ),
    AbstractionContract(
        "architecture.dependency_policy", "tools/architecture/dependency_policy", "Checks dependency direction and public boundaries",
        ("Module membership is explicit", "New and stale exceptions fail",
         "Qt capability is granted only to named modules"),
        ("tools/architecture/dependency_policy/policy.py",),
    ),
    AbstractionContract(
        "architecture.check_dependencies", "tools/architecture/commands/check_dependencies", "Runs dependency checks",
        ("Source and configured evidence are distinguished", "Checking does not configure or build"),
        ("tools/architecture/commands/check_dependencies/__main__.py",),
    ),
    AbstractionContract(
        "source.foundation", "src/foundation", "Groups toolkit-independent primitives",
        ("Children own complete implementations and public exports",), grouping=True,
    ),
    AbstractionContract(
        "source.core.borrowed", "src/foundation/borrowed_reference", "Move-only reference with external ownership",
        ("Moving empties the source", "Referent lifetime remains external", "Only the owned public subtree is exported"),
        ("src/foundation/borrowed_reference/public/wiola/foundation/borrowed.hpp",),
    ),
)
