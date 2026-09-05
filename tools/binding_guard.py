#!/usr/bin/env python3
"""Validate Python and .NET native declarations against the public C ABI."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import abi_guard

ROOT = Path(__file__).resolve().parents[1]
PYTHON_BINDING = ROOT / "python" / "mkvcodec" / "_native_signatures.py"
DOTNET_BINDING = ROOT / "dotnet" / "MkvCodec" / "NativeMethods.Generated.cs"


class BindingGuardError(RuntimeError):
    """Raised when a language binding and the C ABI symbol set differ."""


def _binding_symbols(path: Path, pattern: str) -> set[str]:
    """Extract unique native function names from one binding source.

    Parameters
    ----------
    path : pathlib.Path
        Binding source file.
    pattern : str
        Regular expression containing one symbol capture group.

    Returns
    -------
    set of str
        Declared ``mkvc_*`` entry points.
    """
    return set(re.findall(pattern, path.read_text(encoding="utf-8")))


def validate(
    header: Path = abi_guard.HEADER,
    python_binding: Path = PYTHON_BINDING,
    dotnet_binding: Path = DOTNET_BINDING,
) -> None:
    """Require each language binding to declare every public ABI function.

    Parameters
    ----------
    header : pathlib.Path
        Canonical C ABI header.
    python_binding : pathlib.Path
        ctypes declaration module.
    dotnet_binding : pathlib.Path
        P/Invoke declaration source.

    Raises
    ------
    BindingGuardError
        If either binding omits or invents an exported symbol.
    """
    expected = set(abi_guard._surface(header)["functions"])
    bindings = {
        "Python": _binding_symbols(python_binding, r"lib\.(mkvc_\w+)"),
        ".NET": _binding_symbols(
            dotnet_binding, r"\bextern\s+[^;()]+?\b(mkvc_\w+)\s*\("),
    }
    failures = []
    for language, actual in bindings.items():
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        if missing:
            failures.append(f"{language} missing: {', '.join(missing)}")
        if extra:
            failures.append(f"{language} extra: {', '.join(extra)}")
    if failures:
        raise BindingGuardError("; ".join(failures))


def main() -> int:
    """Run fail-closed language-binding validation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check",))
    parser.parse_args()
    try:
        validate()
    except (BindingGuardError, abi_guard.AbiGuardError, OSError) as error:
        print(f"binding-guard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
