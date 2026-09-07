#!/usr/bin/env python3
# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0
"""Measure what Tessera costs the compiler.

For a library that does all of its work during translation, "performance" means compile time and
compiler memory. This script compiles ``benchmarks/compile_time/bench_tu.cpp`` once per
(implementation, component count, algebra implementation) and records the wall-clock time and the
peak resident set size of the compiler process itself -- measured per process with ``os.wait4``,
not sampled, so the numbers are exact and reproducible.

Usage:
    python3 benchmarks/run_compile_bench.py                       # defaults, current compiler
    python3 benchmarks/run_compile_bench.py --compiler g++ --sizes 32,64,128
    python3 benchmarks/run_compile_bench.py --backends portable,builtin --repeats 3
    python3 benchmarks/run_compile_bench.py --only algebra --sizes 1000,10000,100000 \
            --timeout 1800 --memory-cap 32768        # the scaling sweep

A configuration that cannot be compiled is a result too, so every way of failing is recorded and
reported rather than raised: `depth` (past the compiler's template instantiation limit), `steps`
(past its constant-evaluation budget), `oom` (past --memory-cap), `timeout` (past --timeout).
"""

from __future__ import annotations

import argparse
import os
import resource
import shutil
import signal
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
    # Deduplication with no container built on top: the only implementation that survives to the
    # component counts where the two algebra implementations actually diverge.
    "algebra": 4,
    # The same input with no deduplication at all -- the floor to subtract from "algebra".
    "setup": 5,
    # Transitive dependency resolution with a topological sort, i.e. what `tessera::resolve` costs
    # over `assembly`, which flattens a list that is already complete.
    "resolve": 6,
    # The same resolution over a chain, where depth equals size: the instantiation-depth limit.
    "resolve_chain": 7,
}

BACKEND_DEPENDENT = ("assembly", "algebra", "resolve", "resolve_chain")

BACKEND_MACRO = {
    "auto": None,
    "portable": "TESSERA_ALGEBRA_BACKEND=TESSERA_ALGEBRA_PORTABLE",
    "builtin": "TESSERA_ALGEBRA_BACKEND=TESSERA_ALGEBRA_BUILTIN",
    "fold": "TESSERA_ALGEBRA_BACKEND=TESSERA_ALGEBRA_FOLD",
    # Needs a toolchain with P2996; pass the compiler's reflection flag through --extra.
    "reflection": "TESSERA_ALGEBRA_BACKEND=TESSERA_ALGEBRA_REFLECTION",
}


class RunResult:
    """One compiler invocation: seconds of wall clock and megabytes of peak RSS."""

    def __init__(self, seconds: float, peak_mib: float, ok: bool, log: str = "", failure: str = "") -> None:
        self.seconds = seconds
        self.peak_mib = peak_mib
        self.ok = ok
        self.log = log
        self.failure = failure


# Every wall has its own diagnostic, and they are worth telling apart: three of the four are raised
# by a *default flag* and move if you raise it, while `oom` and `truncated` do not.
FAILURE_SIGNATURES = (
    ("depth", ("exceeded maximum depth", "template instantiation depth")),
    ("nesting", ("exceeded expression nesting limit",)),
    ("steps", ("constexpr evaluation hit maximum step limit", "maximum number of operations",
               "exceeded maximum number of steps")),
    ("oom", ("out of memory", "bad_alloc", "cannot allocate", "std::bad_alloc")),
    ("crash", ("please submit a bug report", "stack dump:", "segmentation fault")),
    # The static_assert on the list length is the only thing standing between a silently truncated
    # pack (LLVM #119600) and a benchmark that measures the wrong list -- see docs/benchmarks.md.
    ("truncated", ("static assertion failed",)),
)


def classify(log: str, timed_out: bool) -> str:
    """Why a compilation did not finish. Each of these is a real wall, not an accident."""
    if timed_out:
        return "timeout"
    lowered = log.lower()
    for name, signatures in FAILURE_SIGNATURES:
        if any(signature in lowered for signature in signatures):
            return name
    return "failed"


def first_error(log: str) -> str:
    """The first `error:` line, so a failure never has to be reproduced by hand to be understood."""
    for line in log.splitlines():
        if "error:" in line:
            return line.strip()[:300]
    return ""


def measure(command: list[str], timeout: float | None = None, memory_cap_mib: int | None = None,
            stack_mib: int | None = None) -> RunResult:
    """Run *command*, returning its wall time and its own peak resident memory.

    The compiler is forked directly rather than run through ``subprocess`` so that ``os.wait4``
    reports *its* resource usage: ``ru_maxrss`` is the process's exact high-water mark, not a
    sample. ``--memory-cap`` is enforced in the child with ``RLIMIT_AS``, which turns a run that
    would otherwise swap the machine into an ordinary allocation failure.
    """
    with tempfile.TemporaryFile() as output:
        start = time.monotonic()
        pid = os.fork()
        if pid == 0:  # child
            try:
                if memory_cap_mib:
                    cap = memory_cap_mib * 1024 * 1024
                    resource.setrlimit(resource.RLIMIT_AS, (cap, cap))
                if stack_mib:
                    limit = resource.RLIM_INFINITY if stack_mib < 0 else stack_mib * 1024 * 1024
                    resource.setrlimit(resource.RLIMIT_STACK, (limit, limit))
                os.dup2(output.fileno(), 1)
                os.dup2(output.fileno(), 2)
                os.execvp(command[0], command)
            except Exception:  # pragma: no cover - only reachable if exec fails
                os._exit(127)

        timed_out = False
        while True:
            waited, status, usage = os.wait4(pid, os.WNOHANG)
            if waited == pid:
                break
            if timeout is not None and time.monotonic() - start > timeout:
                os.kill(pid, signal.SIGKILL)
                _, status, usage = os.wait4(pid, 0)
                timed_out = True
                break
            time.sleep(0.05)

        seconds = time.monotonic() - start
        output.seek(0)
        log = output.read().decode(errors="replace")

    ok = (not timed_out) and status == 0
    failure = ""
    if not ok:
        failure = classify(log, timed_out)
        # A compiler killed by SIGSEGV usually prints nothing at all: a fold expression over tens of
        # thousands of arguments exhausts the parser's stack long before any resource limit is hit,
        # which is why --stack exists.
        if failure == "failed" and os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGSEGV:
            failure = "crash"
    return RunResult(seconds, usage.ru_maxrss / 1024.0, ok, log, failure)


def compile_command(compiler: str, std: str, impl: int, size: int, backend: str, extra: list[str],
                    include_dir: Path = INCLUDE_DIR) -> list[str]:
    command = [
        *compiler.split(),
        f"-std={std}",
        "-c",
        str(BENCH_TU),
        "-o",
        os.devnull,
        f"-I{include_dir}",
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
    parser.add_argument("--only", default="", help="comma-separated subset of implementations to run "
                                                   f"({', '.join(IMPLEMENTATIONS)})")
    parser.add_argument("--timeout", type=float, default=None, help="seconds before a compilation is killed")
    parser.add_argument("--memory-cap", type=int, default=None,
                        help="MiB of address space the compiler may use before allocation fails")
    parser.add_argument("--include", default=str(INCLUDE_DIR),
                        help="include directory to measure (for A/B-ing two implementations of a header)")
    parser.add_argument("--stack", type=int, default=None,
                        help="MiB of stack for the compiler (-1 = unlimited); needed past ~3000 components")
    arguments = parser.parse_args()

    sizes = [int(value) for value in arguments.sizes.split(",")]
    backends = [value.strip() for value in arguments.backends.split(",")]
    extra = arguments.extra.split()

    for backend in backends:
        if backend not in BACKEND_MACRO:
            parser.error(f"unknown backend '{backend}' (expected one of {', '.join(BACKEND_MACRO)})")

    selected = [value.strip() for value in arguments.only.split(",") if value.strip()] or list(IMPLEMENTATIONS)
    for name in selected:
        if name not in IMPLEMENTATIONS:
            parser.error(f"unknown implementation '{name}' (expected one of {', '.join(IMPLEMENTATIONS)})")

    if shutil.which(arguments.compiler.split()[0]) is None:
        parser.error(f"compiler not found: {arguments.compiler}")

    columns: list[str] = []
    times: dict = {}
    memory: dict = {}

    for name in selected:
        impl = IMPLEMENTATIONS[name]
        # Only the implementations that actually call the algebra depend on which one is selected;
        # measuring the others once per backend would just repeat the same number.
        applicable = backends if name in BACKEND_DEPENDENT else ["auto"]
        for backend in applicable:
            column = name if backend == "auto" else f"{name} ({backend})"
            columns.append(column)
            for size in sizes:
                command = compile_command(arguments.compiler, arguments.std, impl, size, backend, extra,
                                          Path(arguments.include))
                best: RunResult | None = None
                failure = ""
                for _ in range(arguments.repeats):
                    result = measure(command, arguments.timeout, arguments.memory_cap, arguments.stack)
                    if not result.ok:
                        # A configuration that cannot be compiled is a result too, and *how* it
                        # fails is the interesting part: the linear fold runs out of instantiation
                        # depth, the reflection implementation runs out of constexpr steps, and the
                        # template implementations run out of memory — at very different N.
                        failure = result.failure
                        detail = first_error(result.log)
                        if detail:
                            print(f"      {detail}", file=sys.stderr)
                        break
                    if best is None or result.seconds < best.seconds:
                        best = result
                if failure:
                    times[(column, size)] = failure
                    memory[(column, size)] = failure
                    print(f"  {column:<26} N={size:<8} {failure}", file=sys.stderr)
                    continue
                assert best is not None
                times[(column, size)] = f"{best.seconds:.2f}"
                memory[(column, size)] = f"{best.peak_mib:.0f}"
                print(f"  {column:<26} N={size:<8} {best.seconds:8.2f} s  {best.peak_mib:8.0f} MiB", file=sys.stderr)

    print(f"<!-- generated by benchmarks/run_compile_bench.py -->")
    print(f"Compiler: `{compiler_version(arguments.compiler)}`, `-std={arguments.std} {' '.join(extra)}`, "
          f"best of {arguments.repeats}.\n")
    print(render_table("Compile time", sizes, columns, times, "seconds"))
    print(render_table("Peak compiler memory", sizes, columns, memory, "MiB (peak RSS of the compiler process)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
