# Qt storage test support

Atomic output implementations provide controlled short writes, failures and gated completion for Qt storage/application/widget tests. Each helper owns only its test interaction state. They implement the real AtomicOutput contract and require no generated fixture registry or Python selector.
