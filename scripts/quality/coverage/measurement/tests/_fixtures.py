from pathlib import Path
import tempfile
import unittest

from scripts.build.cmake import CMakeInventory
from scripts.quality.coverage.measurement.report import CoverageMeasurement
from scripts.quality.coverage.source_policy.policy import SourcePolicy, SourceRecord


class MeasurementFixture(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.policy = SourcePolicy(
            [
                SourceRecord(
                    "src/primitive/public/value.hpp", "primitive", "cpp", "production"
                ),
                SourceRecord(
                    "src/primitive/tests/value_test.cpp", "primitive", "cpp", "test"
                ),
                SourceRecord("tools/reader/read.py", "reader", "python", "tooling"),
                SourceRecord(
                    "tools/reader/tests/test_read.py", "reader", "python", "test"
                ),
                SourceRecord("scripts/sdk.sh", "sdk", "shell", "tooling"),
            ],
            [],
        )
        self.measurement = CoverageMeasurement(self.policy, self.root)
        self.inventory = CMakeInventory(self.root, ())

    def raw(self, path="src/primitive/public/value.hpp", total=10, covered=7):
        return {
            "data": [
                {
                    "files": [
                        {
                            "filename": str(self.root / path),
                            "summary": {
                                metric: {"count": total, "covered": covered}
                                for metric in (
                                    "lines",
                                    "functions",
                                    "regions",
                                    "branches",
                                    "instantiations",
                                )
                            },
                        }
                    ]
                }
            ]
        }
