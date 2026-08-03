#!/usr/bin/env python3

import argparse
import datetime
import gzip
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import uuid


SCHEMA_VERSION = 1
BUFFER_SIZE = 1024 * 1024


class SymbolToolError(RuntimeError):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(BUFFER_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value):
    encoded = json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def utc_now():
    return (
        datetime.datetime.now(datetime.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def require_regular_file(path, label):
    path = pathlib.Path(path)
    if path.is_symlink() or not path.is_file():
        raise SymbolToolError(f"{label} must be a regular file: {path}")
    return path.resolve()


def require_directory(path, label):
    path = pathlib.Path(path)
    if path.is_symlink() or not path.is_dir():
        raise SymbolToolError(f"{label} must be a directory: {path}")
    return path.resolve()


def resolve_executable(command):
    if os.sep in command or (os.altsep and os.altsep in command):
        candidate = pathlib.Path(command)
        resolved = str(candidate.resolve()) if candidate.exists() else None
    else:
        resolved = shutil.which(command)
    if not resolved or not os.path.isfile(resolved) or not os.access(resolved, os.X_OK):
        raise SymbolToolError(f"Required executable is unavailable: {command}")
    return resolved


def run_tool(command, timeout_seconds=120):
    executable = resolve_executable(command[0])
    invocation = [executable, *command[1:]]
    try:
        result = subprocess.run(
            invocation,
            capture_output=True,
            check=False,
            text=True,
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as error:
        raise SymbolToolError(
            f"Command timed out after {timeout_seconds}s: {executable}"
        ) from error
    if result.returncode != 0:
        details = (result.stderr or result.stdout).strip()
        raise SymbolToolError(
            f"Command failed ({result.returncode}): {executable}: {details}"
        )
    return result, executable


def parse_dwarfdump_uuids(output):
    records = {}
    pattern = re.compile(
        r"^UUID:\s+([0-9A-Fa-f-]+)\s+\(([^)]+)\)\s+(.+?)\s*$"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        try:
            value = str(uuid.UUID(match.group(1))).upper()
        except ValueError as error:
            raise SymbolToolError(f"Invalid Mach-O UUID from dwarfdump: {line}") from error
        architecture = match.group(2).strip()
        if not architecture:
            raise SymbolToolError(f"Missing Mach-O architecture from dwarfdump: {line}")
        previous = records.get(architecture)
        if previous and previous != value:
            raise SymbolToolError(
                f"Conflicting Mach-O UUIDs for architecture {architecture}"
            )
        records[architecture] = value
    if not records:
        raise SymbolToolError("dwarfdump returned no Mach-O UUIDs")
    return [
        {"architecture": architecture, "uuid": records[architecture]}
        for architecture in sorted(records)
    ]


def macho_uuids(paths, dwarfdump, timeout_seconds=120):
    combined = {}
    executable = None
    for path in paths:
        result, executable = run_tool(
            [dwarfdump, "--uuid", str(path)], timeout_seconds=timeout_seconds
        )
        for record in parse_dwarfdump_uuids(result.stdout):
            architecture = record["architecture"]
            previous = combined.get(architecture)
            if previous and previous != record["uuid"]:
                raise SymbolToolError(
                    f"Debug artifact has conflicting UUIDs for {architecture}"
                )
            combined[architecture] = record["uuid"]
    return (
        [
            {"architecture": architecture, "uuid": combined[architecture]}
            for architecture in sorted(combined)
        ],
        executable,
    )


def dsym_files(dsym):
    dwarf_root = dsym / "Contents" / "Resources" / "DWARF"
    if not dwarf_root.is_dir() or dwarf_root.is_symlink():
        raise SymbolToolError(f"dSYM has no DWARF directory: {dsym}")
    files = []
    for path in sorted(dwarf_root.rglob("*")):
        if path.is_symlink():
            raise SymbolToolError(f"dSYM symlinks are not accepted: {path}")
        if path.is_file():
            files.append(path)
        elif not path.is_dir():
            raise SymbolToolError(f"dSYM contains unsupported file type: {path}")
    if not files:
        raise SymbolToolError(f"dSYM has no DWARF files: {dsym}")
    return files


def bundle_inventory(dsym):
    records = []
    for root, directory_names, file_names in os.walk(dsym, followlinks=False):
        directory_names.sort()
        file_names.sort()
        root_path = pathlib.Path(root)
        for name in directory_names:
            path = root_path / name
            if path.is_symlink():
                raise SymbolToolError(f"dSYM symlinks are not accepted: {path}")
        for name in file_names:
            path = root_path / name
            if path.is_symlink() or not path.is_file():
                raise SymbolToolError(f"dSYM contains unsupported file type: {path}")
            records.append(
                {
                    "path": path.relative_to(dsym).as_posix(),
                    "size": path.stat().st_size,
                    "sha256": sha256_file(path),
                }
            )
    if not records:
        raise SymbolToolError(f"dSYM bundle is empty: {dsym}")
    return records


def tar_paths(dsym):
    paths = [dsym]
    for root, directory_names, file_names in os.walk(dsym, followlinks=False):
        directory_names.sort()
        file_names.sort()
        root_path = pathlib.Path(root)
        paths.extend(root_path / name for name in directory_names)
        paths.extend(root_path / name for name in file_names)
    return paths


def add_tar_path(archive, path, parent):
    relative = path.relative_to(parent).as_posix()
    metadata = path.lstat()
    info = tarfile.TarInfo(relative)
    info.mode = stat.S_IMODE(metadata.st_mode)
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    if stat.S_ISDIR(metadata.st_mode):
        info.type = tarfile.DIRTYPE
        info.size = 0
        archive.addfile(info)
        return
    if not stat.S_ISREG(metadata.st_mode):
        raise SymbolToolError(f"dSYM contains unsupported file type: {path}")
    info.type = tarfile.REGTYPE
    info.size = metadata.st_size
    with path.open("rb") as source:
        archive.addfile(info, source)


def create_deterministic_archive(dsym, destination):
    with pathlib.Path(destination).open("xb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(
                fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
            ) as archive:
                for path in tar_paths(dsym):
                    add_tar_path(archive, path, dsym.parent)


def build_identity_input(binary_record, source_record):
    return {
        "schemaVersion": SCHEMA_VERSION,
        "platform": "macos",
        "binary": {
            "name": binary_record["name"],
            "size": binary_record["size"],
            "sha256": binary_record["sha256"],
            "machoUuids": binary_record["machoUuids"],
        },
        "sourceBuildManifestSha256": (
            source_record["sha256"] if source_record else None
        ),
    }


def source_manifest_record(path, binary_sha256):
    if path is None:
        raise SymbolToolError("Source build manifest is required")
    path = require_regular_file(path, "Source build manifest")
    try:
        with path.open(encoding="utf-8") as source:
            contents = json.load(source)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise SymbolToolError(f"Invalid source build manifest: {path}: {error}") from error
    native_sha256 = contents.get("artifacts", {}).get("native")
    if contents.get("version") != 1 or not re.fullmatch(
        r"[0-9a-f]{64}", native_sha256 or ""
    ):
        raise SymbolToolError(
            "Source build manifest lacks version 1 artifacts.native SHA-256"
        )
    if native_sha256 != binary_sha256:
        raise SymbolToolError(
            "Source build manifest native artifact does not match binary SHA-256"
        )
    return {
        "name": path.name,
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
        "nativeArtifactSha256": native_sha256,
    }


def safe_prefix(value):
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip(".-")
    if not cleaned:
        raise SymbolToolError("Output prefix has no safe characters")
    return cleaned


def write_json(path, value):
    with pathlib.Path(path).open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, ensure_ascii=False, indent=2, sort_keys=True)
        output.write("\n")


def publish_files(staged, output_dir):
    destinations = []
    for source in staged:
        destination = output_dir / source.name
        if destination.exists() or destination.is_symlink():
            raise SymbolToolError(f"Refusing to overwrite output: {destination}")
        destinations.append(destination)
    published = []
    try:
        for source, destination in zip(staged, destinations, strict=True):
            os.link(source, destination)
            published.append(destination)
    except OSError as error:
        raise SymbolToolError(f"Could not publish symbol archive: {error}") from error
    return published


def create_macos_archive(
    binary,
    dsym,
    output_dir,
    dwarfdump="dwarfdump",
    source_build_manifest=None,
    prefix=None,
    timeout_seconds=120,
):
    binary = require_regular_file(binary, "Binary")
    dsym = require_directory(dsym, "dSYM")
    if dsym.suffix != ".dSYM":
        raise SymbolToolError(f"Debug bundle must end in .dSYM: {dsym}")
    output_dir = pathlib.Path(output_dir)
    if output_dir.exists() and (output_dir.is_symlink() or not output_dir.is_dir()):
        raise SymbolToolError(f"Output directory is invalid: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    output_dir = output_dir.resolve()

    binary_uuid_records, dwarfdump_path = macho_uuids(
        [binary], dwarfdump, timeout_seconds
    )
    dwarf_paths = dsym_files(dsym)
    dsym_uuid_records, _ = macho_uuids(dwarf_paths, dwarfdump_path, timeout_seconds)
    if dsym_uuid_records != binary_uuid_records:
        raise SymbolToolError(
            "Binary and dSYM Mach-O UUIDs differ: "
            f"binary={binary_uuid_records}, dsym={dsym_uuid_records}"
        )

    binary_record = {
        "name": binary.name,
        "size": binary.stat().st_size,
        "sha256": sha256_file(binary),
        "machoUuids": binary_uuid_records,
    }
    source_record = source_manifest_record(
        source_build_manifest, binary_record["sha256"]
    )
    identity_input = build_identity_input(binary_record, source_record)
    identity = canonical_sha256(identity_input)
    output_prefix = safe_prefix(prefix or binary.name)
    base_name = f"{output_prefix}-{identity[:20]}"
    archive_name = f"{base_name}.dSYM.tar.gz"
    manifest_name = f"{base_name}.manifest.json"
    checksum_name = f"{base_name}.sha256"

    with tempfile.TemporaryDirectory(prefix=".symbol-archive-", dir=output_dir) as temp:
        temporary = pathlib.Path(temp)
        archive_path = temporary / archive_name
        manifest_path = temporary / manifest_name
        checksum_path = temporary / checksum_name
        create_deterministic_archive(dsym, archive_path)
        archive_record = {
            "fileName": archive_name,
            "format": "tar+gzip",
            "size": archive_path.stat().st_size,
            "sha256": sha256_file(archive_path),
        }
        manifest = {
            "schemaVersion": SCHEMA_VERSION,
            "kind": "macos-dsym-archive",
            "createdAt": utc_now(),
            "buildIdentity": {
                "algorithm": "sha256",
                "value": identity,
                "input": identity_input,
            },
            "binary": binary_record,
            "debugArtifact": {
                "bundleName": dsym.name,
                "archive": archive_record,
                "files": bundle_inventory(dsym),
                "dwarfFiles": [
                    path.relative_to(dsym).as_posix() for path in dwarf_paths
                ],
                "machoUuids": dsym_uuid_records,
            },
            "sourceBuildManifest": source_record,
            "tools": {
                "dwarfdump": {
                    "path": dwarfdump_path,
                    "sha256": sha256_file(dwarfdump_path),
                }
            },
        }
        write_json(manifest_path, manifest)
        checksums = [
            (archive_record["sha256"], archive_name),
            (sha256_file(manifest_path), manifest_name),
        ]
        with checksum_path.open("x", encoding="utf-8", newline="\n") as output:
            for digest, name in checksums:
                output.write(f"{digest}  {name}\n")
        archive_out, manifest_out, checksum_out = publish_files(
            [archive_path, manifest_path, checksum_path], output_dir
        )
    return {
        "status": "PASS",
        "buildIdentity": identity,
        "archive": str(archive_out),
        "manifest": str(manifest_out),
        "checksums": str(checksum_out),
    }


def load_manifest(path):
    path = require_regular_file(path, "Symbol manifest")
    try:
        with path.open(encoding="utf-8") as source:
            manifest = json.load(source)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise SymbolToolError(f"Invalid symbol manifest: {path}: {error}") from error
    if (
        manifest.get("schemaVersion") != SCHEMA_VERSION
        or manifest.get("kind") != "macos-dsym-archive"
    ):
        raise SymbolToolError("Unsupported symbol manifest schema or kind")
    return path, manifest


def companion_paths(manifest_path, manifest, archive, checksums):
    archive_path = (
        pathlib.Path(archive)
        if archive
        else manifest_path.parent / manifest["debugArtifact"]["archive"]["fileName"]
    )
    if checksums:
        checksum_path = pathlib.Path(checksums)
    elif manifest_path.name.endswith(".manifest.json"):
        checksum_path = manifest_path.with_name(
            manifest_path.name[: -len(".manifest.json")] + ".sha256"
        )
    else:
        raise SymbolToolError("Checksum path is required for nonstandard manifest name")
    return (
        require_regular_file(archive_path, "dSYM archive"),
        require_regular_file(checksum_path, "Checksum sidecar"),
    )


def verify_checksums(checksum_path, expected):
    records = {}
    pattern = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")
    with checksum_path.open(encoding="utf-8") as source:
        for raw_line in source:
            line = raw_line.rstrip("\n")
            match = pattern.fullmatch(line)
            if not match or match.group(2) in records:
                raise SymbolToolError(f"Invalid checksum sidecar: {checksum_path}")
            records[match.group(2)] = match.group(1)
    if records != expected:
        raise SymbolToolError(
            f"Checksum sidecar does not match exact artifacts: {checksum_path}"
        )


def safe_member_name(name, bundle_name):
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise SymbolToolError(f"Unsafe archive member path: {name}")
    if path.parts[0] != bundle_name:
        raise SymbolToolError(f"Archive member is outside expected dSYM: {name}")
    return path


def extract_archive(archive_path, destination, bundle_name):
    destination = pathlib.Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    if any(destination.iterdir()):
        raise SymbolToolError(f"Extraction directory must be empty: {destination}")
    seen = set()
    with tarfile.open(archive_path, mode="r:gz") as archive:
        for member in archive.getmembers():
            member_path = safe_member_name(member.name, bundle_name)
            normalized = member_path.as_posix().rstrip("/")
            if normalized in seen:
                raise SymbolToolError(f"Duplicate archive member: {member.name}")
            seen.add(normalized)
            target = destination.joinpath(*member_path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=False)
                continue
            if not member.isreg():
                raise SymbolToolError(f"Unsupported archive member type: {member.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise SymbolToolError(f"Could not read archive member: {member.name}")
            with target.open("xb") as output:
                shutil.copyfileobj(source, output, BUFFER_SIZE)
            os.chmod(target, member.mode & 0o777)
    bundle = destination / bundle_name
    if not bundle.is_dir():
        raise SymbolToolError(f"Archive does not contain dSYM bundle: {bundle_name}")
    return bundle


def validate_source_manifest(expected, supplied):
    if expected is None:
        if supplied is not None:
            raise SymbolToolError(
                "Archive is not bound to a source build manifest; supplied manifest rejected"
            )
        return None
    if supplied is None:
        raise SymbolToolError("Exact source build manifest is required")
    actual = source_manifest_record(supplied, expected["nativeArtifactSha256"])
    if actual != expected:
        raise SymbolToolError(
            f"Source build manifest identity mismatch: expected={expected}, actual={actual}"
        )
    return actual


def validate_extracted_bundle(bundle, manifest, dwarfdump, timeout_seconds):
    expected = manifest["debugArtifact"]
    actual_inventory = bundle_inventory(bundle)
    if actual_inventory != expected["files"]:
        raise SymbolToolError("Extracted dSYM file inventory or SHA-256 differs")
    dwarf_paths = []
    for relative in expected["dwarfFiles"]:
        pure = pathlib.PurePosixPath(relative)
        if pure.is_absolute() or ".." in pure.parts:
            raise SymbolToolError(f"Unsafe DWARF path in manifest: {relative}")
        dwarf_paths.append(require_regular_file(bundle.joinpath(*pure.parts), "DWARF file"))
    uuids, _ = macho_uuids(dwarf_paths, dwarfdump, timeout_seconds)
    if uuids != expected["machoUuids"]:
        raise SymbolToolError(
            f"Extracted dSYM UUID mismatch: expected={expected['machoUuids']}, actual={uuids}"
        )
    return uuids


def validate_macos_archive(
    manifest_path,
    binary,
    archive=None,
    checksums=None,
    dwarfdump="dwarfdump",
    source_build_manifest=None,
    extract_dir=None,
    timeout_seconds=120,
):
    manifest_path, manifest = load_manifest(manifest_path)
    archive_path, checksum_path = companion_paths(
        manifest_path, manifest, archive, checksums
    )
    binary = require_regular_file(binary, "Binary")
    expected_archive = manifest["debugArtifact"]["archive"]
    expected_checksums = {
        archive_path.name: sha256_file(archive_path),
        manifest_path.name: sha256_file(manifest_path),
    }
    verify_checksums(checksum_path, expected_checksums)
    if (
        archive_path.name != expected_archive["fileName"]
        or archive_path.stat().st_size != expected_archive["size"]
        or expected_checksums[archive_path.name] != expected_archive["sha256"]
    ):
        raise SymbolToolError("dSYM archive name, size, or SHA-256 mismatch")

    actual_uuids, _ = macho_uuids([binary], dwarfdump, timeout_seconds)
    actual_binary = {
        "name": binary.name,
        "size": binary.stat().st_size,
        "sha256": sha256_file(binary),
        "machoUuids": actual_uuids,
    }
    if actual_binary != manifest["binary"]:
        raise SymbolToolError(
            f"Exact binary identity mismatch: expected={manifest['binary']}, actual={actual_binary}"
        )
    actual_source = validate_source_manifest(
        manifest.get("sourceBuildManifest"), source_build_manifest
    )
    identity_input = build_identity_input(actual_binary, actual_source)
    identity = canonical_sha256(identity_input)
    expected_identity = manifest["buildIdentity"]
    if (
        expected_identity.get("algorithm") != "sha256"
        or expected_identity.get("input") != identity_input
        or expected_identity.get("value") != identity
    ):
        raise SymbolToolError("Build identity record does not match exact inputs")
    if manifest["debugArtifact"]["machoUuids"] != actual_uuids:
        raise SymbolToolError("dSYM UUID set does not match exact binary UUID set")

    def validate_at(destination):
        bundle = extract_archive(
            archive_path, destination, manifest["debugArtifact"]["bundleName"]
        )
        validate_extracted_bundle(bundle, manifest, dwarfdump, timeout_seconds)
        return bundle

    if extract_dir is None:
        with tempfile.TemporaryDirectory(prefix="symbol-validate-") as temporary:
            validate_at(temporary)
        extracted_bundle = None
    else:
        extracted_bundle = validate_at(extract_dir)
    return {
        "status": "PASS",
        "buildIdentity": identity,
        "binarySha256": actual_binary["sha256"],
        "archiveSha256": expected_archive["sha256"],
        "manifestSha256": expected_checksums[manifest_path.name],
        "machoUuids": actual_uuids,
        "extractedBundle": str(extracted_bundle) if extracted_bundle else None,
    }


def parse_module_line(symbols):
    first_line = symbols.splitlines()[0] if symbols.splitlines() else ""
    match = re.fullmatch(r"MODULE\s+(\S+)\s+(\S+)\s+([0-9A-Fa-f]+)\s+(.+)", first_line)
    if not match:
        raise SymbolToolError("dump_syms output has no valid MODULE header")
    return {
        "os": match.group(1),
        "architecture": match.group(2),
        "debugId": match.group(3).upper(),
        "name": match.group(4),
    }


def symbol_name_matches(actual, expected):
    if actual == expected:
        return True
    if not actual.startswith(expected):
        return False
    return actual[len(expected) : len(expected) + 1] in {"(", " ", "[", "+"}


def symbol_file_has_expected_frame(symbols, expected_frame):
    patterns = [
        re.compile(r"^FUNC(?: m)? [0-9A-Fa-f]+ [0-9A-Fa-f]+ [0-9A-Fa-f]+ (.+)$"),
        re.compile(r"^PUBLIC(?: m)? [0-9A-Fa-f]+ [0-9A-Fa-f]+ (.+)$"),
    ]
    for line in symbols.splitlines():
        for pattern in patterns:
            match = pattern.match(line)
            if match and symbol_name_matches(match.group(1), expected_frame):
                return True
    return False


def matching_stack_frame(output, expected_module, expected_frame):
    frame_prefix = re.compile(r"^\s*(?:\d+|frame\s+#?\d+)\s+", re.IGNORECASE)
    module_marker = f"{expected_module}!"
    for line in output.splitlines():
        if not frame_prefix.match(line):
            continue
        marker_index = line.find(module_marker)
        if marker_index < 0:
            continue
        symbol = line[marker_index + len(module_marker) :].strip()
        if symbol.startswith(("??", "0x")):
            continue
        if symbol_name_matches(symbol, expected_frame):
            return line.strip()
    return None


def gate_minidump(
    manifest_path,
    binary,
    minidump,
    expected_module,
    expected_frame,
    dump_syms,
    minidump_stackwalk,
    archive=None,
    checksums=None,
    dwarfdump="dwarfdump",
    source_build_manifest=None,
    architecture=None,
    timeout_seconds=120,
):
    minidump = require_regular_file(minidump, "Minidump")
    if not expected_module or any(character in expected_module for character in "\r\n!"):
        raise SymbolToolError("Expected module must be a nonempty literal module name")
    if not expected_frame or any(character in expected_frame for character in "\r\n"):
        raise SymbolToolError("Expected frame must be a nonempty literal symbol name")
    dump_syms_path = resolve_executable(dump_syms)
    stackwalk_path = resolve_executable(minidump_stackwalk)

    with tempfile.TemporaryDirectory(prefix="symbol-gate-") as temporary:
        temporary_path = pathlib.Path(temporary)
        extract_dir = temporary_path / "extracted"
        validation = validate_macos_archive(
            manifest_path=manifest_path,
            binary=binary,
            archive=archive,
            checksums=checksums,
            dwarfdump=dwarfdump,
            source_build_manifest=source_build_manifest,
            extract_dir=extract_dir,
            timeout_seconds=timeout_seconds,
        )
        uuid_records = validation["machoUuids"]
        if architecture is None:
            if len(uuid_records) != 1:
                raise SymbolToolError(
                    "--architecture is required for a multi-architecture binary"
                )
            architecture = uuid_records[0]["architecture"]
        matching_uuid = next(
            (
                record["uuid"]
                for record in uuid_records
                if record["architecture"] == architecture
            ),
            None,
        )
        if matching_uuid is None:
            raise SymbolToolError(
                f"Requested architecture is absent from exact artifact: {architecture}"
            )
        dsym = pathlib.Path(validation["extractedBundle"])
        symbols_result, _ = run_tool(
            [dump_syms_path, "-a", architecture, str(dsym)], timeout_seconds
        )
        symbols = symbols_result.stdout
        module = parse_module_line(symbols)
        expected_debug_id = matching_uuid.replace("-", "") + "0"
        if module != {
            "os": "mac",
            "architecture": architecture,
            "debugId": expected_debug_id,
            "name": expected_module,
        }:
            raise SymbolToolError(
                f"dump_syms MODULE identity mismatch: expected mac/{architecture}/"
                f"{expected_debug_id}/{expected_module}, actual={module}"
            )
        if not symbol_file_has_expected_frame(symbols, expected_frame):
            raise SymbolToolError(
                f"Expected frame is absent from exact dump_syms output: {expected_frame}"
            )

        symbol_root = temporary_path / "breakpad-symbols"
        symbol_directory = symbol_root / module["name"] / module["debugId"]
        symbol_directory.mkdir(parents=True)
        symbol_file = symbol_directory / f"{module['name']}.sym"
        symbol_file.write_text(symbols, encoding="utf-8", newline="\n")
        stackwalk_result, _ = run_tool(
            [stackwalk_path, str(minidump), str(symbol_root)], timeout_seconds
        )
        stack_output = stackwalk_result.stdout + "\n" + stackwalk_result.stderr
        frame_line = matching_stack_frame(
            stack_output, expected_module, expected_frame
        )
        if frame_line is None:
            raise SymbolToolError(
                f"Minidump did not resolve required frame: "
                f"{expected_module}!{expected_frame}"
            )
    return {
        "status": "PASS",
        "buildIdentity": validation["buildIdentity"],
        "minidumpSha256": sha256_file(minidump),
        "module": module,
        "expectedFrame": f"{expected_module}!{expected_frame}",
        "resolvedFrame": frame_line,
        "tools": {
            "dumpSyms": {
                "path": dump_syms_path,
                "sha256": sha256_file(dump_syms_path),
            },
            "minidumpStackwalk": {
                "path": stackwalk_path,
                "sha256": sha256_file(stackwalk_path),
            },
        },
    }


def positive_timeout(value):
    timeout = int(value)
    if timeout <= 0:
        raise argparse.ArgumentTypeError("timeout must be positive")
    return timeout


def add_validation_arguments(parser):
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--archive")
    parser.add_argument("--checksums")
    parser.add_argument("--binary", required=True)
    parser.add_argument("--source-build-manifest", required=True)
    parser.add_argument("--dwarfdump", default="dwarfdump")
    parser.add_argument("--timeout-seconds", type=positive_timeout, default=120)


def build_parser():
    parser = argparse.ArgumentParser(
        description="Fail-closed Brave symbol archive and symbolication tooling"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    archive = subparsers.add_parser("archive-macos")
    archive.add_argument("--binary", required=True)
    archive.add_argument("--dsym", required=True)
    archive.add_argument("--output-dir", required=True)
    archive.add_argument("--source-build-manifest", required=True)
    archive.add_argument("--prefix")
    archive.add_argument("--dwarfdump", default="dwarfdump")
    archive.add_argument("--timeout-seconds", type=positive_timeout, default=120)

    validate = subparsers.add_parser("validate-macos")
    add_validation_arguments(validate)

    gate = subparsers.add_parser("gate-minidump-macos")
    add_validation_arguments(gate)
    gate.add_argument("--minidump", required=True)
    gate.add_argument("--expected-module", required=True)
    gate.add_argument("--expected-frame", required=True)
    gate.add_argument("--architecture")
    gate.add_argument("--dump-syms", required=True)
    gate.add_argument("--minidump-stackwalk", required=True)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.command == "archive-macos":
            result = create_macos_archive(
                binary=args.binary,
                dsym=args.dsym,
                output_dir=args.output_dir,
                dwarfdump=args.dwarfdump,
                source_build_manifest=args.source_build_manifest,
                prefix=args.prefix,
                timeout_seconds=args.timeout_seconds,
            )
        elif args.command == "validate-macos":
            result = validate_macos_archive(
                manifest_path=args.manifest,
                binary=args.binary,
                archive=args.archive,
                checksums=args.checksums,
                dwarfdump=args.dwarfdump,
                source_build_manifest=args.source_build_manifest,
                timeout_seconds=args.timeout_seconds,
            )
        else:
            result = gate_minidump(
                manifest_path=args.manifest,
                binary=args.binary,
                minidump=args.minidump,
                expected_module=args.expected_module,
                expected_frame=args.expected_frame,
                dump_syms=args.dump_syms,
                minidump_stackwalk=args.minidump_stackwalk,
                archive=args.archive,
                checksums=args.checksums,
                dwarfdump=args.dwarfdump,
                source_build_manifest=args.source_build_manifest,
                architecture=args.architecture,
                timeout_seconds=args.timeout_seconds,
            )
    except (SymbolToolError, KeyError, TypeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
