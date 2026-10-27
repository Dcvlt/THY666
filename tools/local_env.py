"""Read private local configuration without executing or exporting its contents.

Callers keep explicit CLI arguments first. Nonempty process environment values
override the workspace .env; empty values fall back to the caller's default.
Quoted values are literal, including Windows backslashes and dollar signs.
"""
from __future__ import annotations

import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
KEY = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def parse_env(text: str) -> dict[str, str]:
    result = {}
    for number, raw in enumerate(text.lstrip("\ufeff").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        line = re.sub(r"^export[ \t]+", "", line)
        key, separator, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if not separator or not KEY.fullmatch(key) or key in result:
            raise ValueError(f"Invalid or duplicate local configuration key at line {number}")
        if value.startswith(("'", '"')):
            quote = value[0]
            end = value.find(quote, 1)
            if end < 0 or (value[end + 1:].strip() and not value[end + 1:].strip().startswith("#")):
                raise ValueError(f"Invalid quoted local configuration at line {number}")
            value = value[1:end]
        else:
            value = re.split(r"[ \t]+#", value, maxsplit=1)[0].rstrip()
        result[key] = value
    return result


def read_env(path: Path | str | None = None) -> dict[str, str]:
    source = Path(path) if path is not None else ROOT / ".env"
    if not source.exists():
        return {}
    return parse_env(source.read_text(encoding="utf-8-sig"))


def env_value(key: str, default: str | None = None) -> str | None:
    value = os.environ.get(key)
    if value is None or not value.strip():
        value = read_env().get(key)
    return value if value is not None and value.strip() else default


def env_path(key: str, default: Path | str | None = None) -> Path | None:
    value = env_value(key)
    if value is None:
        return Path(default) if default is not None else None
    path = Path(value).expanduser()
    return (path if path.is_absolute() else ROOT / path).resolve()


def env_int(key: str, default: int, minimum: int, maximum: int) -> int:
    value = env_value(key)
    if value is None:
        return default
    if not re.fullmatch(r"[0-9]+", value) or not minimum <= int(value) <= maximum:
        raise ValueError(f"{key} must be an integer in {minimum}..{maximum}")
    return int(value)
