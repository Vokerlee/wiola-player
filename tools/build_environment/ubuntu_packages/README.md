# Ubuntu package archives

`UbuntuPackages` owns APT interaction. `PackagePlan` records the requested packages and resolved archive identities; `PackageArchive` validates one filename, byte size and checksum. This adapter has no GitHub dependency or built-in GUI/compiler selection.

```mermaid
flowchart LR
  Requirements[Caller package requirements] --> Resolve[APT update and read-only resolution]
  Resolve --> Plan[PackagePlan and content identity]
  Plan --> Restore[Caller restores archive cache]
  Restore --> Install[APT verifies archives and installs dependencies]
  Install --> Retain[Retain only this transaction's archives]
  Retain --> Save[Caller saves successful cache]
```

Run `python3 -m tools.build_environment.ubuntu_packages plan --output /tmp/packages.json -- PACKAGE...` to update indexes, resolve dependencies into an empty temporary archive directory, save the plan, and print its SHA-256 identity. Resolver diagnostics go to stderr. Identical requested packages and resolved filenames/sizes/checksums produce the same identity regardless of mirror URLs or request order. Package versions and dependencies come from APT's current indexes rather than a manually maintained list.

After restoring a cache, run `python3 -m tools.build_environment.ubuntu_packages install --plan /tmp/packages.json --archives /path/to/dedicated-cache`. APT verifies restored packages, downloads missing archives and installs dependencies with recommendations disabled. The dedicated cache stays outside system APT directories. Only obsolete top-level `.deb` files in that caller-selected directory are removed, after a successful install. Failures propagate and cannot publish successful installation evidence. Cache transport and saving only after success belong to the caller. Plans and installs use the same runner and package indexes; a plan is an archive reuse identity, not an independently installable system image.

The GitHub action uses default-branch caches so pull requests can reuse downloaded tools without creating duplicate caches. A cache miss retains the ordinary APT installation path. Bounded inactivity retries handle failed transfers; archive caching avoids repeating slow but continuously progressing downloads.
