# Shared callback contracts

`shared_callback_test` verifies independently owned targets through reentrant replacement and owner destruction, shared mutable state, empty/moved values, exception propagation and reference/move-only payload forwarding. `consumer.cpp` is a standard-only client built even when GoogleTest suites are disabled. Allocation and real-time behavior are not part of this notification abstraction’s contract.
