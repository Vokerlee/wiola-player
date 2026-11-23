"""The desktop contract's fixture variables, without process-global changes."""

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


@dataclass(frozen=True)
class DesktopEnvironment:
    artifacts: Path
    media: Path | None = None
    qt_platform: str | None = None

    def apply(self, inherited: Mapping[str, str]) -> dict[str, str]:
        result = dict(inherited)
        result["WIOLA_GUI_FAILURE_ARTIFACTS"] = str(self.artifacts.resolve())
        if self.media is not None:
            if not self.media.is_dir():
                raise ValueError(
                    "Missing desktop media fixture directory: " + str(self.media)
                )
            result["WIOLA_JOURNEY_MEDIA_DIR"] = str(self.media.resolve())
        if self.qt_platform is not None:
            result["QT_QPA_PLATFORM"] = self.qt_platform
        return result
