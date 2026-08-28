#!/usr/bin/env python3
# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0
"""Measure what Tessera costs the compiler.

For a library that does all of its work during translation, "performance" means compile time and
compiler memory. This script compiles ``benchmarks/compile_time/bench_tu.cpp`` once per
(implementation, component count, deduplication backend) and records the wall-clock time and the
peak resident set size of the compiler process itself -- measured per process with ``os.wait4``,
not sampled, so the numbers are exact and reproducible.

Usage:
    python3 benchmarks/run_compile_bench.py                       # defaults, current compiler
    python3 benchmarks/run_compile_bench.py --compiler g++ --sizes 32,64,128
    python3 benchmarks/run_compile_bench.py --backends portable,builtin --repeats 3
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BENCH_TU = REPO_ROOT / "benchmarks" / "compile_time" / "bench_tu.cpp"
INCLUDE_DIR = REPO_ROOT / "include"

IMPLEMENTATIONS = {
    "headers only": 0,
    "std::tuple": 1,
    "mosaic": 2,
    "assembly": 3,
}

BACKEND_MACRO = {
    "auto": None,
    "portable": "TESSERA_DEDUP_BACKEND=TESSERA_DEDUP_PORTABLE",
    "builtin": "TESSERA_DEDUP_BACKEND=TESSERA_DEDUP_BUILTIN",
    "fold": "TESSERA_DEDUP_BACKEND=TESSERA_DEDUP_FOLD",
    # Needs a toolchain with P2996; pass the compiler's reflection flag through --extra.
    "reflection": "TESSERA_DEDUP_BACKEND=TESSERA_DEDUP_REFLECTION",
}


class RunResult:
    """One compiler invocation: seconds of wall clock and megabytes of peak RSS."""

    def __init__(self, seconds: float, peak_mib: float, ok: bool, log: str = "") -> None:
        self.seconds = seconds
        self.peak_mib = peak_mib
        self.ok = ok
        self.log = log


def measure(command: list[str]) -> RunResult:
    """Run *command*, returning its wall time and its own peak resident memory."""
    with tempfile.TemporaryFile() as output:
        start = time.monotonic()
        pid = os.fork()
        if pid == 0:  # child
            try:
                os.dup2(output.fileno(), 1)
                os.dup2(output.fileno(), 2)
                os.execvp(command[0], command)
            except Exception:  # pragma: no cover - only reachable if exec fails
                os._exit(127)
        _, status, usage = os.wait4(pid, 0)
        seconds = time.monotonic() - start
        output.seek(0)
        log = output.read().decode(errors="replace")
    return RunResult(seconds, usage.ru_maxrss / 1024.0, status == 0, log)


def compile_command(compiler: str, std: str, impl: int, size: int, backend: str, extra: list[str]) -> list[str]:
    command = [
        *compiler.split(),
        f"-std={std}",
        "-c",
        str(BENCH_TU),
        "-o",
        os.devnull,
        f"-I{INCLUDE_DIR}",
        f"-DTESSERA_BENCH_IMPL={impl}",
        f"-DTESSERA_BENCH_N={size}",
    ]
    macro = BACKEND_MACRO[backend]
    if macro:
        command.append(f"-D{macro}")
    if backend == "reflection":
        command.append("-DTESSERA_ENABLE_REFLECTION_BACKEND=1")
    command.extend(extra)
    return command


def compiler_version(compiler: str) -> str:
    try:
        first_line = subprocess.run([*compiler.split(), "--version"], capture_output=True, text=True,
                                    check=False).stdout.splitlines()
        return first_line[0] if first_line else compiler
    except OSError:
        return compiler


def render_table(title: str, sizes: list[int], columns: list[str], cells: dict, unit: str) -> str:
    header = "| components | " + " | ".join(columns) + " |"
    ruler = "|---" * (len(columns) + 1) + "|"
    lines = [f"### {title}", "", header, ruler]
    for size in sizes:
        row = [f"| {size:>10} "]
        for column in columns:
            value = cells.get((column, size))
            row.append(f"| {value:>{max(len(column), 6)}} " if value is not None else "| n/a ")
        lines.append("".join(row) + "|")
    lines.append("")
    lines.append(f"_Values in {unit}; lower is better._")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"), help="compiler command (default: $CXX)")
    parser.add_argument("--std", default="c++23", help="language standard (default: c++23)")
    parser.add_argument("--sizes", default="16,32,64,128,256", help="comma-separated component counts")
    parser.add_argument("--backends", default="auto", help="comma-separated: auto, portable, builtin, fold, reflection")
    parser.add_argument("--repeats", type=int, default=3, help="runs per configuration; the fastest one counts")
    parser.add_argument("--extra", default="-O0", help="extra compiler flags")
    arguments = parser.parse_args()

    sizes = [int(value) for value in arguments.sizes.split(",")]
    backends = [value.strip() for value in arguments.backends.split(",")]
    extra = arguments.extra.split()

    for backend in backends:
        if backend not in BACKEND_MACRO:
            parser.error(f"unknown backend '{backend}' (expected one of {', '.join(BACKEND_MACRO)})")

    if shutil.which(arguments.compiler.split()[0]) is None:
        parser.error(f"compiler not found: {arguments.compiler}")

    columns: list[str] = []
    times: dict = {}
    memory: dict = {}

    for name, impl in IMPLEMENTATIONS.items():
        # Only the assembling implementation depends on the deduplication backend; measuring the
        # others once per backend would just repeat the same number.
        applicable = backends if impl == IMPLEMENTATIONS["assembly"] else ["auto"]
        for backend in applicable:
            column = name if backend == "auto" else f"{name} ({backend})"
            columns.append(column)
            for size in sizes:
                command = compile_command(arguments.compiler, arguments.std, impl, size, backend, extra)
                best: RunResult | None = None
                failure = ""
                for _ in range(arguments.repeats):
                    result = measure(command)
                    if not result.ok:
                        # A configuration that cannot be compiled is a result too: the linear fold
                        # runs out of instantiation depth long before the other backends do.
                        failure = "depth" if "maximum depth" in result.log else "failed"
                        break
                    if best is None or result.seconds < best.seconds:
                        best = result
                if failure:
                    times[(column, size)] = failure
                    memory[(column, size)] = failure
                    print(f"  {column:<26} N={size:<5} {failure}", file=sys.stderr)
                    continue
                assert best is not None
                times[(column, size)] = f"{best.seconds:.2f}"
                memory[(column, size)] = f"{best.peak_mib:.0f}"
                print(f"  {column:<26} N={size:<5} {best.seconds:6.2f} s  {best.peak_mib:6.0f} MiB", file=sys.stderr)

    print(f"<!-- generated by benchmarks/run_compile_bench.py -->")
    print(f"Compiler: `{compiler_version(arguments.compiler)}`, `-std={arguments.std} {' '.join(extra)}`, "
          f"best of {arguments.repeats}.\n")
    print(render_table("Compile time", sizes, columns, times, "seconds"))
    print(render_table("Peak compiler memory", sizes, columns, memory, "MiB (peak RSS of the compiler process)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
