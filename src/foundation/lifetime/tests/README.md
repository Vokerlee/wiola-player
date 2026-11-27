# Lifetime graph contracts

The unit suite verifies non-null construction, non-empty value copies, weak acquisition, const/member projections, interface deletion, construction failure, multi-input ownership and capability transfer. Static assertions reject borrowed construction and access through temporary leases. The standard-only consumer proves that a dependent reader remains valid after its original input lease is released.
