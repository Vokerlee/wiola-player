"""Existing Ubuntu host build requirements; CI only supplies explicit choices."""


def host_packages(qt: str, additional: str) -> tuple[str, ...]:
    if qt not in {"true", "false"}:
        raise ValueError("Host Qt selection must be true or false")
    packages = ["ccache"]
    if qt == "true":
        packages.extend(("qt6-base-dev", "qt6-qpa-plugins"))
    packages.extend(additional.split())
    return tuple(dict.fromkeys(packages))
