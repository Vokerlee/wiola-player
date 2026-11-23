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
    AbstractionContract(
        'testing.package', 'tools/testing', 'Groups independent test-policy tooling',
        ("Complete implementation and tests belong to this owner",),
        (), grouping=True,
    ),
    AbstractionContract(
        'testing.catalog', 'tools/testing/catalog', 'Reconciles test ownership, semantic purpose and applicability',
        ("Complete implementation and tests belong to this owner",),
        ('tools/testing/catalog/catalog.py',), grouping=False,
    ),
    AbstractionContract(
        'architecture.check_tests', 'tools/architecture/commands/check_tests', 'Assembles configured test-contract reconciliation',
        ("Complete implementation and tests belong to this owner",),
        ('tools/architecture/commands/check_tests/__main__.py',), grouping=False,
    ),
    AbstractionContract(
        'testing.compatibility_corpus', 'tests/compatibility', 'Qualifies persisted client-data compatibility using independent input oracles',
        ("Complete implementation and tests belong to this owner",),
        (), grouping=False,
    ),
    AbstractionContract(
        'coverage.package', 'tools/coverage', 'Groups independent measurement and qualification components',
        ('Grouping contains no runtime measurement implementation',),
        (), grouping=True,
    ),
    AbstractionContract(
        'coverage.source_policy', 'tools/coverage/source_policy', 'Preserves explicit measurement applicability and source identity',
        ('Every executable source has explicit applicability', 'Moves preserve owner identity and historical counts', 'Language roles cannot hide maintained tooling'),
        ('tools/coverage/source_policy/policy.py',), grouping=False,
    ),
    AbstractionContract(
        'coverage.measurement', 'tools/coverage/measurement', 'Reconciles raw coverage with owned source and configured execution',
        ('Configured production translation units have mappings', 'Declared Python tools include zero-hit statement mappings', 'Test code never inflates production counts', 'Unsupported languages stay visibly unmeasured'),
        ('tools/coverage/measurement/report.py',), grouping=False,
    ),
    AbstractionContract(
        'coverage.python_runner', 'tools/coverage/python_runner', 'Collects Python tool coverage with retained process evidence',
        ('Every recorded command succeeds', 'Instrumented source bytes remain stable during collection', 'Process evidence is retained and hashed'),
        ('tools/coverage/python_runner/__main__.py',), grouping=False,
    ),
    AbstractionContract(
        'sdk.package', 'tools/sdk', 'Groups independent external SDK builders',
        ("Complete implementation and tests belong to this owner",),
        (), grouping=True,
    ),
    AbstractionContract(
        'sdk.qt_mingw', 'tools/sdk/qt_mingw', 'Builds and identifies the x86-64 static Qt SDK',
        ("Complete implementation and tests belong to this owner",),
        ('tools/sdk/qt_mingw/build.sh', 'tools/sdk/qt_mingw/context.cmake'), grouping=False,
    ),
    AbstractionContract(
        "ci.environment_setup", ".github/actions/setup-build", "Restores caches and installs selected Ubuntu host dependencies",
        ("Cache presence is not build or test success", "Headless builds do not require host Qt", "Package inputs are argument arrays"),
        (".github/actions/setup-build/action.yml",),
    ),
    AbstractionContract(
        'build_environment.package', 'tools/build_environment', 'Groups independent host build-environment adapters',
        ("Complete implementation and tests belong to this owner",),
        (), grouping=True,
    ),
    AbstractionContract(
        'build_environment.ubuntu_packages', 'tools/build_environment/ubuntu_packages', 'Resolves and installs Ubuntu packages with reusable verified archives',
        ("Complete implementation and tests belong to this owner",),
        ('tools/build_environment/ubuntu_packages/packages.py', 'tools/build_environment/ubuntu_packages/__main__.py'), grouping=False,
    ),
    AbstractionContract(
        'architecture.review_contract', 'tools/architecture/review_contract', 'Validates review contracts and repository subject conventions',
        ("Complete implementation and tests belong to this owner",),
        ('tools/architecture/review_contract/review.py',), grouping=False,
    ),
    AbstractionContract(
        'architecture.policy_guard', 'tools/architecture/policy_guard', 'Rejects silent weakening of accepted architecture policy',
        ("Complete implementation and tests belong to this owner",),
        ('tools/architecture/policy_guard/policy.py',), grouping=False,
    ),
    AbstractionContract(
        'architecture.check_design', 'tools/architecture/commands/check_design', 'Assembles trusted design acceptance and existing architecture checks',
        ("Complete implementation and tests belong to this owner",),
        ('tools/architecture/commands/check_design/__main__.py',), grouping=False,
    ),
    AbstractionContract(
        'testing.gtest', 'tools/testing/gtest', 'Executes the GoogleTest discovery and report protocol with complete case evidence',
        ("Complete implementation and tests belong to this owner",),
        ('tools/testing/gtest/runner.py', 'tools/testing/gtest/report.py'), grouping=False,
    ),
    AbstractionContract(
        'testing.desktop_checks', 'tools/testing/desktop_checks', 'Composes desktop fixture environment and packaged test execution',
        ("Complete implementation and tests belong to this owner",),
        ('tools/testing/desktop_checks/__main__.py', 'tools/testing/desktop_checks/environment.py'), grouping=False,
    ),
    AbstractionContract(
        'testing.cmake_build', 'tools/testing/cmake_build', 'Builds configured CMake targets and resolves their declared executable artifacts',
        ("Complete implementation and tests belong to this owner",),
        ('tools/testing/cmake_build/build.py',), grouping=False,
    ),
)
