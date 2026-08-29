#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "build" / "docgen-src"
SITE_OUTPUT = ROOT / "build" / "docsite"

ID_PATTERNS = {
    "external": re.compile(r"\bEXT-[A-Z]+-\d{3}\b"),
    "acceptance": re.compile(r"\bAC-[A-Z]+-\d{3}\b"),
    "internal": re.compile(r"\bINT-[A-Z]+-\d{3}\b"),
    "tests": re.compile(r"\bTEST-[A-Z]+-\d{3}\b"),
}

SOURCES = {
    "external": ROOT / "docs" / "external-spec" / "system-spec.md",
    "internal": ROOT / "docs" / "internal-spec" / "system-design.md",
    "tests": ROOT / "docs" / "test-spec" / "test-requirements.md",
}


class DocgenError(RuntimeError):
    pass


def read_text(path: Path) -> str:
    if not path.is_file():
        raise DocgenError(f"required input is missing: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def id_sets() -> dict[str, set[str]]:
    result = {
        name: set(ID_PATTERNS[name].findall(read_text(path)))
        for name, path in SOURCES.items()
    }
    result["acceptance"] = set(
        ID_PATTERNS["acceptance"].findall(read_text(SOURCES["external"]))
    )
    return result


def validate() -> tuple[dict[str, object], dict[str, object], dict[str, set[str]]]:
    model = json.loads(read_text(ROOT / "docs" / "design-model.json"))
    gate = json.loads(read_text(ROOT / "docs" / "quality-gate.json"))
    ids = id_sets()
    trace = read_text(ROOT / "docs" / "traceability.md")

    expected_metrics = {
        "external_requirements": len(ids["external"]),
        "acceptance_criteria": len(ids["acceptance"]),
        "internal_design_rules": len(ids["internal"]),
        "test_requirements": len(ids["tests"]),
    }
    metrics = gate.get("metrics", {})
    mismatches = [
        f"{name}: quality-gate={metrics.get(name)!r}, actual={actual}"
        for name, actual in expected_metrics.items()
        if metrics.get(name) != actual
    ]
    for kind, values in ids.items():
        missing = sorted(value for value in values if value not in trace)
        if missing:
            mismatches.append(f"traceability missing {kind}: {', '.join(missing)}")

    model_external = {
        requirement
        for group in model.get("requirement_groups", [])
        for requirement in group.get("ids", [])
    }
    missing_model = sorted(ids["external"] - model_external)
    if missing_model:
        mismatches.append(
            "design-model missing external: " + ", ".join(missing_model)
        )
    failures = gate.get("failures", [])
    if failures:
        mismatches.append("quality gate failures: " + "; ".join(map(str, failures)))
    if mismatches:
        raise DocgenError("\n".join(mismatches))
    return model, gate, ids


def c_api_reference() -> str:
    header = read_text(ROOT / "include" / "mkvcodec" / "mkvc.h")
    declarations = re.findall(
        r"MKVC_API\s+(.+?)\s+(mkvc_[a-zA-Z0-9_]+)\s*\((.*?)\);",
        header,
        flags=re.DOTALL,
    )
    lines = ["# C ABI reference", "", "Source: `include/mkvcodec/mkvc.h`.", ""]
    for return_type, name, arguments in declarations:
        signature = " ".join(
            f"{return_type} {name}({arguments})".replace("\n", " ").split()
        )
        lines.extend([f"## `{name}`", "", f"```c\n{signature};\n```", ""])
    if not declarations:
        raise DocgenError("no MKVC_API declarations found")
    return "\n".join(lines)


def python_api_reference() -> str:
    module_path = ROOT / "python" / "mkvcodec" / "_api.py"
    tree = ast.parse(read_text(module_path), filename=str(module_path))
    lines = ["# Python API reference", "", "Source: `python/mkvcodec/_api.py`.", ""]
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name.startswith("_"):
            continue
        lines.extend([f"## `{node.name}`", ""])
        for member in node.body:
            if not isinstance(member, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            if member.name.startswith("_") and member.name != "__init__":
                continue
            arguments = ast.unparse(member.args)
            returns = (
                f" -> {ast.unparse(member.returns)}" if member.returns is not None else ""
            )
            lines.extend(
                [
                    f"### `{member.name}`",
                    "",
                    f"```python\n{member.name}({arguments}){returns}\n```",
                    "",
                ]
            )
    return "\n".join(lines)


def trace_matrix(model: dict[str, object]) -> str:
    lines = [
        "# Generated traceability matrix",
        "",
        "Generated from `docs/design-model.json`. The canonical detailed mapping is",
        "`docs/traceability.md`.",
        "",
        "| Domain | External | Acceptance | Internal | Tests |",
        "|---|---|---|---|---|",
    ]
    for group in model["requirement_groups"]:
        cells = [
            group["domain"],
            "<br>".join(group["ids"]),
            "<br>".join(group["acceptance"]),
            "<br>".join(group["internal"]),
            "<br>".join(group["tests"]),
        ]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def quality_report(gate: dict[str, object], ids: dict[str, set[str]]) -> str:
    lines = [
        "# Specification quality report",
        "",
        f"Result: **{gate['result']}**",
        "",
        "| Item | Count |",
        "|---|---:|",
        f"| External requirements | {len(ids['external'])} |",
        f"| Acceptance criteria | {len(ids['acceptance'])} |",
        f"| Internal design rules | {len(ids['internal'])} |",
        f"| Test requirements | {len(ids['tests'])} |",
        "",
        "## Warnings",
        "",
    ]
    warnings = gate.get("warnings", [])
    lines.extend(f"- {warning}" for warning in warnings)
    if not warnings:
        lines.append("- None")
    return "\n".join(lines) + "\n"


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.rstrip() + "\n", encoding="utf-8", newline="\n")


def generate(output: Path) -> None:
    model, gate, ids = validate()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    copies = {
        ROOT / "docs" / "README.md": output / "specification" / "index.md",
        SOURCES["external"]: output / "specification" / "external.md",
        SOURCES["internal"]: output / "specification" / "internal.md",
        SOURCES["tests"]: output / "specification" / "tests.md",
        ROOT / "docs" / "traceability.md": output / "specification" / "traceability.md",
        ROOT / "docs" / "implementation-status.md":
            output / "implementation-status.md",
        ROOT / "docs" / "docgen.md": output / "docgen.md",
        ROOT / "LICENSE_POLICY.md": output / "license-policy.md",
    }
    for source, destination in copies.items():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    specification_index = output / "specification" / "index.md"
    write_text(
        specification_index,
        read_text(ROOT / "docs" / "README.md").replace(
            "(implementation-status.md)", "(../implementation-status.md)"
        ),
    )

    gate_metrics = gate["metrics"]
    index = f"""# MKVCodec documentation

This site is generated by `docgen` from the repository's canonical
specification, source API declarations, and quality-gate model.

## Current specification

| Metric | Value |
|---|---:|
| External requirements | {gate_metrics['external_requirements']} |
| Acceptance criteria | {gate_metrics['acceptance_criteria']} |
| Internal design rules | {gate_metrics['internal_design_rules']} |
| Test requirements | {gate_metrics['test_requirements']} |
| Quality gate | {gate['result']} |

Generated files are build artifacts. Edit the canonical files under `docs/`,
`include/`, and `python/` instead.
"""
    write_text(output / "index.md", index)
    write_text(output / "generated" / "traceability-matrix.md", trace_matrix(model))
    write_text(output / "generated" / "quality-report.md", quality_report(gate, ids))
    write_text(output / "api" / "c-abi.md", c_api_reference())
    write_text(output / "api" / "python.md", python_api_reference())


def build(output: Path, site: Path) -> None:
    generate(output)
    subprocess.run(
        [
            sys.executable,
            "-m",
            "mkdocs",
            "build",
            "--strict",
            "--config-file",
            str(ROOT / "mkdocs.yml"),
            "--site-dir",
            str(site),
        ],
        cwd=ROOT,
        check=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(prog="docgen")
    parser.add_argument("command", choices=("generate", "check", "build"))
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--site", type=Path, default=SITE_OUTPUT)
    arguments = parser.parse_args()
    try:
        if arguments.command == "check":
            validate()
            with tempfile.TemporaryDirectory(prefix="mkvcodec-docgen-") as temporary:
                generate(Path(temporary))
        elif arguments.command == "generate":
            generate(arguments.output)
        else:
            build(arguments.output, arguments.site)
    except (DocgenError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"docgen: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
