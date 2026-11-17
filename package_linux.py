"""Linux runtime packaging, invoked by build.py --package."""

from pathlib import Path
import json
import os
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
# These belong to the host OS / graphics driver installation.
HOST_LIBS = re.compile(
    r"^(ld-linux.*|lib(?:c|m|mvec|dl|pthread|rt|resolv|util|anl)\.so\..*|"
    r"libvulkan\.so\..*)$")


def dependencies(executable: Path, env=None) -> dict[str, Path]:
    output = subprocess.check_output(["ldd", str(executable)], text=True, env=env)
    if "not found" in output:
        raise RuntimeError(f"Missing runtime dependency:\n{output}")
    return {name: Path(path) for name, path in
            re.findall(r"^\s*([^\s/]+) => (/\S+) \(", output, re.MULTILINE)}


def copy_licenses(bundle: Path, libraries: dict[str, Path]) -> None:
    destination = bundle / "licenses"
    for path in (ROOT / "third_party").rglob("*"):
        if path.is_file() and path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE")):
            target = destination / "vendored" / path.relative_to(ROOT / "third_party")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    # Installed dependencies may supply notices alongside their libraries.
    for name, path in libraries.items():
        prefix = path.resolve().parent.parent
        sources = [prefix / "share/doc/KTX-Software/html/license.html"] if name.startswith("libktx.") else []
        if shutil.which("pacman"):
            result = subprocess.run(["pacman", "-Qqo", str(path.resolve())],
                                    capture_output=True, text=True)
            if result.returncode == 0:
                sources.append(Path("/usr/share/licenses") / result.stdout.strip())
        for source in sources:
            if source.is_dir():
                shutil.copytree(source, destination / name / source.name, dirs_exist_ok=True)
            elif source.is_file():
                target = destination / name / source.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)


def create_package(build_dir: Path) -> None:
    name = f"blackhole-linux-{platform.machine()}"
    exports = ROOT / "exports"
    exports.mkdir(exist_ok=True)
    print("Packaging Linux runtime...", flush=True)
    with tempfile.TemporaryDirectory(prefix="package-", dir=build_dir) as temporary:
        bundle = Path(temporary) / name
        subprocess.run(["cmake", "--install", str(build_dir), "--config", "Release",
                        "--component", "blackhole", "--prefix", str(bundle)],
                       check=True, stdout=subprocess.DEVNULL)
        executable = bundle / "bin/blackhole"
        # The build executable retains the paths needed to locate dependencies.
        cache = (build_dir / "CMakeCache.txt").read_text()
        output_root = ROOT if "VULKAN_ENGINE_OUTPUT_TO_SOURCE_ROOT:BOOL=ON" in cache else build_dir
        libraries = {name: path for name, path in dependencies(output_root / "bin/blackhole").items()
                     if not HOST_LIBS.match(name)}
        lib_dir = bundle / "bin/lib"
        lib_dir.mkdir()
        for library, source in libraries.items():
            shutil.copy2(source, lib_dir / library)
        launcher = bundle / "run.sh"
        launcher.write_text('''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$root/bin/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VKG_ASSET_ROOT="$root"
export VKG_SHADER_ROOT="$root/bin/shaders"
cd "$root"
exec "$root/bin/blackhole" "$@"
''')
        launcher.chmod(0o755)
        env = dict(os.environ, LD_LIBRARY_PATH=str(lib_dir))
        resolved = dependencies(executable, env)
        for library in libraries:
            if resolved.get(library) != lib_dir / library:
                raise RuntimeError(f"Packaged library did not resolve locally: {library}")
        dynamic = subprocess.check_output(["readelf", "-d", str(executable)], text=True)
        paths = [line for line in dynamic.splitlines() if "RUNPATH" in line or "RPATH" in line]
        if len(paths) != 1 or "[$ORIGIN/lib]" not in paths[0]:
            raise RuntimeError(f"Unexpected package library search path: {paths}")
        if not list((bundle / "bin/shaders").rglob("*.spv")):
            raise RuntimeError("Package has no compiled shaders")
        copy_licenses(bundle, libraries)
        (bundle / "manifest.json").write_text(json.dumps({
            "platform": name, "libraries": sorted(libraries),
            "host_requirements": ["Vulkan loader and GPU driver", "AVX2 CPU",
                                  f"glibc {platform.libc_ver()[1]} or compatible"],
        }, indent=2) + "\n")
        archive = Path(temporary) / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            output.add(bundle, arcname=name)
        destination = exports / archive.name
        archive.replace(destination)
        print(f"Package: {destination}\nExtract and run {name}/run.sh", flush=True)
