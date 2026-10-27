"""Validate recorded build inputs without executing firmware or native binaries."""
from pathlib import Path, PurePosixPath


def require(condition, message):
    if not condition:
        raise ValueError(message)

def artifact_path(parent, name):
    require(isinstance(name, str) and Path(name).name == name and name not in ("", ".", "..") and
            "/" not in name and "\\" not in name,
            "Artifact names must be plain filenames")
    return Path(parent) / name

def verify_additional_native_validations(build, *, root, build_dir, match,
                                         primary_source, primary_executable):
    """Hash-bind optional recorded native runs; never execute their binaries.

    Sources use unambiguous workspace-relative names. Executable names are
    derived from source stems, rather than trusting another manifest path.
    """
    records = build.get("additional_native_validation", [])
    require(isinstance(records, list), "Additional native validation must be a list")
    root, build_dir = Path(root).resolve(), Path(build_dir).resolve()
    require(build_dir.is_relative_to(root), "Additional native build directory leaves workspace")
    key = lambda path: str(Path(path).resolve()).casefold()
    source_keys = {key(root / primary_source)}
    executable_keys = {key(primary_executable)}
    inputs = set()
    for index, record in enumerate(records):
        label = f"Additional native validation {index}"
        require(isinstance(record, dict), label + " record must be an object")
        require(record.get("passed") is True and isinstance(record.get("output"), str) and
                record["output"].strip(), label + " must record a passed run with nonempty output")
        name = record.get("source")
        require(isinstance(name, str) and name and "\\" not in name and ":" not in name and
                not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts and
                PurePosixPath(name).as_posix() == name,
                label + " source must be a safe workspace-relative path")
        source = (root / name).resolve()
        require(source.is_relative_to(root), label + " source leaves workspace")
        executable = artifact_path(build_dir, source.stem + ".exe").resolve()
        require(executable.is_relative_to(build_dir), label + " executable leaves build directory")
        require(key(source) not in source_keys and key(executable) not in executable_keys,
                label + " duplicates a native source or executable")
        source_keys.add(key(source))
        executable_keys.add(key(executable))
        for field in ("source_sha256", "executable_sha256"):
            value = record.get(field)
            require(isinstance(value, str) and len(value) == 64 and
                    all(char in "0123456789abcdef" for char in value),
                    label + " has a malformed " + field)
        match(source, record["source_sha256"], label + " source")
        match(executable, record["executable_sha256"], label + " executable")
        inputs.update((source, executable))
    return inputs, len(records)
