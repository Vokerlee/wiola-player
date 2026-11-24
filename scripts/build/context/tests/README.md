# Build-context contracts

Actual fresh CMake configurations prove that each supported portable selection avoids SDK discovery and fetching, that the combined closure builds only the intended neutral target and that unknown selections fail explicitly. A standard-only Borrowed configuration builds and runs its focused contracts with tests enabled and GoogleTest unavailable. Tests operate in temporary directories and never rewrite the checkout or installed system packages.
