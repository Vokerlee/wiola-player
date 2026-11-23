# Ubuntu package tests

Controlled package-manager processes exercise the real resolver, serialization and installer without network access or system mutation. Oracles cover mirror-independent identity, dependency/version changes, empty installed transactions, rejection before privilege invocation, archive retention and failure propagation. CI provides the real Ubuntu installation and warm-cache checks.
