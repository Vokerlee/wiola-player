# Borrowed references and exclusive handles

This component owns move-only borrowed storage and the exclusive capabilities built on it. `wiola::core::Borrowed<T>` is a nullable pointer to an object owned elsewhere. `Handle<Owner>` transfers a borrowed capability, and `Claimable` records whether its owner has handed that capability out. Their complete templates, public export and tests live together; these related pieces share one target. Namespaces and existing class identities are retained.

```mermaid
stateDiagram-v2
  [*] --> Empty: default construction
  [*] --> Borrowing: construct from T reference
  Borrowing --> Empty: move to another handle
  Empty --> Borrowing: move from a populated handle
```

Moving clears the source; copying is forbidden. Dereference requires a nonempty handle and a live external owner. Destruction never destroys the referent. This value has no synchronization and does not extend object lifetime; cross-thread transfer requires the containing protocol's synchronization.

Claim/release operations belong to the setup/owner thread and are not atomic. Moving a handle clears its previous holder; claiming again while it is held returns an inert capability. A concrete handle defines its own forwarding and release behavior. The referent must outlive every handle; no operation acquires shared lifetime or provides worker synchronization.

Link `wiola::borrowed_reference` and include `<wiola/foundation/borrowed.hpp>` or `<wiola/foundation/handle.hpp>`. Only `public/` is exported; both headers are self-contained and use no legacy macros. The target does not expose the checkout root or depend on `wiola_core`. The same target can be loaded with `find_package(wiola_borrowed_reference CONFIG REQUIRED)` after installation. Existing aggregate consumers temporarily receive this declared target through `wiola_core`; its remaining broad exports are still migration debt. Retired `core/borrowed.hpp` and `core/handle.hpp` paths have no forwarding headers.

[Tests](tests/README.md) verify transfer/aliasing and a relocated installed consumer. Add private helpers inside this owner, outside `public/`, and keep changes compatible with these lifetime guarantees.
