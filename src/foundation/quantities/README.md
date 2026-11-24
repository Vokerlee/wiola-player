# Frequency and time quantities

This component owns the existing `wiola::units` values, dimensional constraints, arithmetic, comparisons, chrono conversions and literals. Time, Frequency and their related unit tags stay together because conversions and consumers form one coherent contract. No class-per-directory or namespace migration is introduced.

```mermaid
flowchart LR
  Consumer[Core or application consumer] --> API[wiola/units.hpp]
  API --> Values[Time and Frequency values]
  Values --> Standard[Standard C++23 only]
  Tests[Focused quantities tests] --> API
```

Link `wiola::quantities` and include `<wiola/units.hpp>`. Public headers expose only this component and standard library types; the target exports its public include directory and language requirement without the source-root umbrella, Qt, codec, device or application dependencies. Component installation is `wiola_quantities`; external consumers use `find_package(wiola_quantities CONFIG REQUIRED)`. Build and install targets have the same namespace/name.

The implementation preserves existing value semantics, reciprocal preconditions and namespaces. Reciprocal conversions require nonzero quantities; runtime assertions and compile-time constraints retain their existing behavior. Private arithmetic stays in the header's detail namespace, where templates can use it without a separate helper component.

Implementation, tests and export definitions share this owner. Historical source and test IDs are explicit parts for measurement continuity. The old global test macro wrapper is retired; its replacement local target definition belongs to quantities and links the public API directly. Legacy player consumers temporarily receive the target through wiola_core; removing that broad source-root export and enabling root configuration without unrelated SDKs remain separate planned work. Independent consumer evidence proves this target's boundary without claiming the whole repository is already isolated.
