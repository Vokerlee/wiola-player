# Lifetime graph contracts

The unit suite verifies non-null construction, non-empty value copies, weak acquisition, const/member projections, interface deletion, construction failure, multi-input ownership and capability transfer. Static assertions reject borrowed construction and access through temporary leases. The standard-only consumer proves that a dependent reader remains valid after its original input lease is released.

Owner-thread observation cases cover empty/default state, explicit/repeated invalidation, scope teardown, copies surviving the scope, reentrant owner deletion, and worker-to-owner delivery. Debug death tests reject access and invalidation on a different thread. The ordinary consumer includes both retained ownership and externally owned scope observation without Qt or other SDKs.
