#!/usr/bin/env python3
"""Configure + build (+ optionally test) the engine.

Usage: build.py [debug|release] [linux|windows] [-q] [--test] [--no-shaders]
    python3 ./build.py -q                # release, Linux clang
    py .\\build.py debug windows --test   # debug + tests, Windows clang-cl
"""

from pathlib import Path
import os, shutil, subprocess, sys

ROOT = Path(__file__).resolve().parent
CONFIGS = {"debug": "Debug", "release": "Release"}
PRESETS = {
    ("windows", "Release"): ("windows-clangcl-release", "build-windows"),
    ("windows", "Debug"): ("windows-clangcl-debug", "build-windows-debug"),
    ("linux", "Release"): ("linux-clang-release", "build-linux"),
    ("linux", "Debug"): ("linux-clang-debug", "build-linux-debug"),
}
def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def ps_quote(value) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def run(cmd: list[str], devshell=None, quiet=False) -> None:
    if not quiet:
        print("+ " + " ".join(cmd), flush=True)
    if devshell:
        powershell, launch = devshell
        invoke = " ".join(ps_quote(arg) for arg in cmd)
        script = (
            f"& {ps_quote(launch)} -Arch amd64 -HostArch amd64 "
            f"-SkipAutomaticLocation | Out-Null; & {invoke}; exit $LASTEXITCODE")
        cmd = [powershell, "-NoProfile", "-Command", script]
    kwargs = dict(cwd=ROOT)
    if quiet:
        result = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
        if result.returncode != 0:
            sys.stdout.write((result.stdout or "") + (result.stderr or ""))
            raise subprocess.CalledProcessError(result.returncode, cmd)
    else:
        subprocess.run(cmd, check=True, **kwargs)


def find_vsdevshell() -> Path | None:
    paths = [Path(os.environ["VSDEVSHELL"])] if os.environ.get("VSDEVSHELL") else []
    for base in (r"C:\Program Files", r"C:\Program Files (x86)"):
        paths.extend((Path(base) / "Microsoft Visual Studio").glob(
            "*/*/Common7/Tools/Launch-VsDevShell.ps1"))
    return next((path for path in paths if path.exists()), None)


def find_vsdev():
    launch = find_vsdevshell()
    if not launch:
        fail("could not find Visual Studio Launch-VsDevShell.ps1")
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    if not powershell:
        fail("could not find PowerShell")
    return powershell, launch


def main() -> int:
    args = sys.argv[1:]

    def pop_flag(*names) -> bool:
        found = any(a in names for a in args)
        args[:] = [a for a in args if a not in names]
        return found

    quiet = pop_flag("-q", "--quiet")
    run_tests = pop_flag("--test", "--tests")
    build_shaders = not pop_flag("--no-shaders")

    config = CONFIGS.get((args[0] if args else "release").lower())
    target = (args[1] if len(args) > 1 else ("windows" if os.name == "nt" else "linux")).lower()
    if not config:
        fail("config must be debug or release")
    if target not in ("linux", "windows"):
        fail("platform must be linux or windows")
    if len(args) > 2:
        fail("too many arguments")
    devshell = find_vsdev() if target == "windows" and os.name == "nt" else None
    if not shutil.which("cmake"):
        fail("missing command: cmake")

    preset, default_dir = PRESETS[(target, config)]
    build_dir = ROOT / default_dir

    configure = ["cmake", "--preset", preset, "--log-level=WARNING", "-Wno-dev",
                 f"-DVULKAN_ENGINE_BUILD_SHADERS={'ON' if build_shaders else 'OFF'}"]

    if run_tests:
        build = ["cmake", "--build", str(build_dir), "--parallel",
                 str(max(1, os.cpu_count() or 1))]
    else:
        build = ["cmake", "--build", "--preset", preset]
    try:
        if quiet:
            print("Configuring...", flush=True)
        run(configure, devshell, quiet)
        if quiet:
            print("Building...", flush=True)
        run(build, devshell, quiet)
        if run_tests:
            if quiet:
                print("Testing...", flush=True)
            run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"], devshell, quiet)
    except subprocess.CalledProcessError as exc:
        return exc.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
