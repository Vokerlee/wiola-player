"""Resolve a configured target artifact through CMake's File API."""

import json
from pathlib import Path


def target_artifact(directory: Path, name: str, kind: str) -> Path:
    """Read one artifact; callers decide whether to build, execute or inspect it."""
    directory = directory.resolve()
    reply = directory / ".cmake/api/v1/reply"

    def read(filename):
        if Path(filename).name != filename or not filename.endswith(".json"):
            raise ValueError("CMake reply references must be local JSON filenames")

        return json.loads((reply / filename).read_text(encoding="utf-8"))

    try:
        indexes = sorted(reply.glob("index-*.json"))
        if not indexes:
            raise ValueError("Configure a CMake File API codemodel-v2 query first")

        index = read(indexes[-1].name)
        model = read(index["reply"]["codemodel-v2"]["jsonFile"])
        if model["version"]["major"] != 2 or len(model["configurations"]) != 1:
            raise ValueError("Select one configured codemodel-v2 build variant")

        targets = [
            entry
            for entry in model["configurations"][0]["targets"]
            if entry["name"] == name
        ]
        if len(targets) != 1:
            raise ValueError("No unique configured CMake target: " + name)

        target = read(targets[0]["jsonFile"])
        artifacts = [
            directory / item["path"]
            for item in target.get("artifacts", [])
            if "nameOnDisk" not in target
            or Path(item["path"]).name == target["nameOnDisk"]
        ]
        if target["type"] != kind or len(artifacts) != 1:
            raise ValueError("No unique configured " + kind + " artifact: " + name)

        return artifacts[0].resolve()
    except (KeyError, TypeError, OSError, json.JSONDecodeError) as error:
        raise ValueError("Invalid or incomplete CMake codemodel: " + str(error)) from error
