#!/usr/bin/env python3

import json
import pathlib
import stat
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).parent))

import archive_symbols


TEST_UUID = "12345678-90AB-CDEF-1234-567890ABCDEF"
OTHER_UUID = "ABCDEF12-3456-7890-ABCD-EF1234567890"


class SymbolArchiveTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="symbol-tool-test-")
        self.root = pathlib.Path(self.temporary.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.dwarfdump = self.make_executable(
            "dwarfdump",
            """#!/usr/bin/env python3
import pathlib
import re
import sys

for value in sys.argv[1:]:
    if value.startswith("-"):
        continue
    path = pathlib.Path(value)
    data = path.read_text(errors="replace")
    uuid = re.search(r"UUID=([0-9A-F-]+)", data).group(1)
    arch = re.search(r"ARCH=([A-Za-z0-9_]+)", data).group(1)
    print(f"UUID: {uuid} ({arch}) {path}")
""",
        )
        self.binary = self.root / "TestModule"
        self.binary.write_text(
            f"UUID={TEST_UUID}\nARCH=arm64\nbinary bytes\n", encoding="utf-8"
        )
        self.dsym = self.root / "TestModule.dSYM"
        dwarf_directory = self.dsym / "Contents" / "Resources" / "DWARF"
        dwarf_directory.mkdir(parents=True)
        (dwarf_directory / "TestModule").write_text(
            f"UUID={TEST_UUID}\nARCH=arm64\ndebug bytes\n", encoding="utf-8"
        )
        (self.dsym / "Contents" / "Info.plist").write_text(
            "<plist></plist>\n", encoding="utf-8"
        )
        self.source_manifest = self.root / "fingerprint-browser-build-manifest.json"
        self.source_manifest.write_text(
            json.dumps(
                {
                    "artifacts": {
                        "native": archive_symbols.sha256_file(self.binary),
                        "nativeSet": {
                            "count": 1,
                            "digest": "1" * 64,
                        },
                    },
                    "version": 1,
                }
            )
            + "\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def make_executable(self, name, content):
        path = self.tools / name
        path.write_text(content, encoding="utf-8", newline="\n")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def archive(self, output_name="archive"):
        return archive_symbols.create_macos_archive(
            binary=self.binary,
            dsym=self.dsym,
            output_dir=self.root / output_name,
            dwarfdump=str(self.dwarfdump),
            source_build_manifest=self.source_manifest,
        )

    def test_parse_dwarfdump_rejects_conflicting_architecture(self):
        output = (
            f"UUID: {TEST_UUID} (arm64) first\n"
            f"UUID: {OTHER_UUID} (arm64) second\n"
        )
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError, "Conflicting Mach-O UUIDs"
        ):
            archive_symbols.parse_dwarfdump_uuids(output)

    def test_archive_and_validate_exact_artifacts(self):
        result = self.archive()
        validation = archive_symbols.validate_macos_archive(
            manifest_path=result["manifest"],
            binary=self.binary,
            dwarfdump=str(self.dwarfdump),
            source_build_manifest=self.source_manifest,
        )
        self.assertEqual("PASS", validation["status"])
        self.assertEqual(result["buildIdentity"], validation["buildIdentity"])
        self.assertEqual(
            [{"architecture": "arm64", "uuid": TEST_UUID}],
            validation["machoUuids"],
        )
        checksums = pathlib.Path(result["checksums"]).read_text(encoding="utf-8")
        self.assertIn(pathlib.Path(result["archive"]).name, checksums)
        self.assertIn(pathlib.Path(result["manifest"]).name, checksums)

    def test_archive_payload_is_deterministic(self):
        first = self.archive("archive-one")
        second = self.archive("archive-two")
        self.assertEqual(
            archive_symbols.sha256_file(first["archive"]),
            archive_symbols.sha256_file(second["archive"]),
        )

    def test_archive_rejects_dsym_uuid_mismatch(self):
        dwarf = self.dsym / "Contents" / "Resources" / "DWARF" / "TestModule"
        dwarf.write_text(
            f"UUID={OTHER_UUID}\nARCH=arm64\ndebug bytes\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError, "Binary and dSYM Mach-O UUIDs differ"
        ):
            self.archive()

    def test_archive_rejects_build_manifest_for_different_binary(self):
        self.source_manifest.write_text(
            json.dumps({"artifacts": {"native": "0" * 64}, "version": 1})
            + "\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError,
            "native artifact does not match binary SHA-256",
        ):
            self.archive()

    def test_validate_rejects_changed_binary(self):
        result = self.archive()
        self.binary.write_text(
            f"UUID={TEST_UUID}\nARCH=arm64\nchanged bytes\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError, "Exact binary identity mismatch"
        ):
            archive_symbols.validate_macos_archive(
                manifest_path=result["manifest"],
                binary=self.binary,
                dwarfdump=str(self.dwarfdump),
                source_build_manifest=self.source_manifest,
            )

    def test_validate_rejects_changed_manifest(self):
        result = self.archive()
        manifest_path = pathlib.Path(result["manifest"])
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["createdAt"] = "2000-01-01T00:00:00Z"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError, "Checksum sidecar"
        ):
            archive_symbols.validate_macos_archive(
                manifest_path=manifest_path,
                binary=self.binary,
                dwarfdump=str(self.dwarfdump),
                source_build_manifest=self.source_manifest,
            )

    def test_minidump_gate_requires_exact_resolved_frame(self):
        result = self.archive()
        dump_syms = self.make_executable(
            "dump_syms",
            f"""#!/usr/bin/env python3
print("MODULE mac arm64 {TEST_UUID.replace('-', '')}0 TestModule")
print("FILE 0 expected.cc")
print("FUNC 1000 20 0 KnownExpectedFrame")
print("1000 20 42 0")
""",
        )
        stackwalk = self.make_executable(
            "minidump_stackwalk",
            """#!/usr/bin/env python3
print("Thread 0 (crashed)")
print(" 0  TestModule!KnownExpectedFrame [expected.cc : 42 + 0x0]")
""",
        )
        minidump = self.root / "known.dmp"
        minidump.write_bytes(b"known minidump")
        gate = archive_symbols.gate_minidump(
            manifest_path=result["manifest"],
            binary=self.binary,
            minidump=minidump,
            expected_module="TestModule",
            expected_frame="KnownExpectedFrame",
            dump_syms=str(dump_syms),
            minidump_stackwalk=str(stackwalk),
            dwarfdump=str(self.dwarfdump),
            source_build_manifest=self.source_manifest,
        )
        self.assertEqual("PASS", gate["status"])
        self.assertIn("KnownExpectedFrame", gate["resolvedFrame"])

    def test_minidump_gate_rejects_expected_text_outside_frame(self):
        result = self.archive()
        dump_syms = self.make_executable(
            "dump_syms",
            f"""#!/usr/bin/env python3
print("MODULE mac arm64 {TEST_UUID.replace('-', '')}0 TestModule")
print("FUNC 1000 20 0 KnownExpectedFrame")
""",
        )
        stackwalk = self.make_executable(
            "minidump_stackwalk",
            """#!/usr/bin/env python3
print("Requested TestModule!KnownExpectedFrame")
print(" 0  TestModule!?? [0x1000]")
""",
        )
        minidump = self.root / "unknown.dmp"
        minidump.write_bytes(b"unknown minidump")
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError,
            "Minidump did not resolve required frame",
        ):
            archive_symbols.gate_minidump(
                manifest_path=result["manifest"],
                binary=self.binary,
                minidump=minidump,
                expected_module="TestModule",
                expected_frame="KnownExpectedFrame",
                dump_syms=str(dump_syms),
                minidump_stackwalk=str(stackwalk),
                dwarfdump=str(self.dwarfdump),
                source_build_manifest=self.source_manifest,
            )

    def test_minidump_gate_rejects_wrong_breakpad_module_id(self):
        result = self.archive()
        dump_syms = self.make_executable(
            "dump_syms",
            f"""#!/usr/bin/env python3
print("MODULE mac arm64 {OTHER_UUID.replace('-', '')}0 TestModule")
print("FUNC 1000 20 0 KnownExpectedFrame")
""",
        )
        stackwalk = self.make_executable(
            "minidump_stackwalk",
            """#!/usr/bin/env python3
print(" 0  TestModule!KnownExpectedFrame")
""",
        )
        minidump = self.root / "wrong-module.dmp"
        minidump.write_bytes(b"wrong module minidump")
        with self.assertRaisesRegex(
            archive_symbols.SymbolToolError, "MODULE identity mismatch"
        ):
            archive_symbols.gate_minidump(
                manifest_path=result["manifest"],
                binary=self.binary,
                minidump=minidump,
                expected_module="TestModule",
                expected_frame="KnownExpectedFrame",
                dump_syms=str(dump_syms),
                minidump_stackwalk=str(stackwalk),
                dwarfdump=str(self.dwarfdump),
                source_build_manifest=self.source_manifest,
            )


if __name__ == "__main__":
    unittest.main()
