#!/usr/bin/env python3
"""Build and install every Animal Audio plugin (everything under plugins/) as VST3.

Cross-platform (macOS, Windows, Linux), standard library only. This is what the /setup agent skill runs,
and it is meant to be usable by hand too:

    python3 scripts/install_plugins.py --prereqs               # check build tools (add --install-prereqs to install them)
    python3 scripts/install_plugins.py --build --clean         # clean Release build of ALL plugins, then install them
    python3 scripts/install_plugins.py                         # install what is already built in build/
    python3 scripts/install_plugins.py --list                  # every copy on this machine, with its build stamp
    python3 scripts/install_plugins.py --uninstall             # remove every Animal Audio plugin
    python3 scripts/install_plugins.py --dry-run ...           # show what would happen, change nothing
    python3 scripts/install_plugins.py --m4l-only              # just the Max for Live devices (no build needed)

Where plugins go (default): macOS ~/Library/Audio/Plug-Ins/VST3, Linux ~/.vst3, Windows
%COMMONPROGRAMFILES%\\VST3 when run as administrator, else %LOCALAPPDATA%\\Programs\\Common\\VST3.
`--system` / `--user` force one; `--prefix DIR` installs into DIR instead (for testing).
Copies of the same plugin in the other standard folders are stale (a host may load the wrong one), so they are
removed unless `--keep-stale`. Each installed bundle's build stamp (version, git SHA, date: embedded by
cmake/GenerateBuildInfo.cmake) is read from the binary and printed, so you can compare it with what the plugin
UI shows (hover the ANIMAL AUDIO badge, or read the small text at the bottom of the left panel).

The Max for Live devices (plugins/*/m4l/*.amxd, checked in) are installed too, into Live's User Library under
Presets/<Audio Effects|Instruments|MIDI Effects>/<Max ...>/Animal Audio, with a Live 12 tag sidecar that files them
under the browser tag Creator > Animal Audio. Live gives the VST3s that same tag itself, from their vendor name.
The User Library is read from Live's Library.cfg (`--user-library` overrides; `--no-m4l` skips). With `--prefix`
(testing) the devices are skipped unless `--user-library` is given.
"""
from __future__ import annotations

import argparse
import ctypes
import os
import platform
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

ROOT = Path(__file__).resolve().parent.parent
MARKER = re.compile(rb"AA-BUILD\|([^|\x00]+)\|([^|\x00]+)\|([^|\x00]+)\|")
MIN_CMAKE = (3, 22)


# ---------------------------------------------------------------- environment

def detect_os() -> str:
    system = platform.system()
    return {"Darwin": "macos", "Windows": "windows"}.get(system, "linux")


def is_admin() -> bool:
    if detect_os() == "windows":
        try:
            return bool(ctypes.windll.shell32.IsUserAnAdmin())  # type: ignore[attr-defined]
        except Exception:
            return False
    return hasattr(os, "geteuid") and os.geteuid() == 0


def standard_dirs(os_name: str) -> dict[str, list[Path]]:
    """The VST3 folders hosts scan, per scope."""
    home = Path.home()
    if os_name == "macos":
        return {"user": [home / "Library/Audio/Plug-Ins/VST3"], "system": [Path("/Library/Audio/Plug-Ins/VST3")]}
    if os_name == "windows":
        common = Path(os.environ.get("COMMONPROGRAMFILES", r"C:\Program Files\Common Files"))
        local = Path(os.environ.get("LOCALAPPDATA", str(home / "AppData/Local")))
        return {"user": [local / "Programs/Common/VST3"], "system": [common / "VST3"]}
    return {"user": [home / ".vst3"], "system": [Path("/usr/lib/vst3"), Path("/usr/local/lib/vst3")]}


def default_scope(os_name: str) -> str:
    return "system" if os_name == "windows" and is_admin() else "user"


# ---------------------------------------------------------------- repo knowledge

def plugin_names() -> list[str]:
    """Base product names from each plugin's CMakeLists (release .vst3 name, without Local suffix)."""
    names = []
    for cmake in sorted((ROOT / "plugins").glob("*/CMakeLists.txt")):
        match = re.search(r'aa_add_plugin\(\s*\w+\s+PRODUCT_NAME\s+"([^"]+)"', cmake.read_text(encoding="utf-8"))
        names.append(match.group(1) if match else cmake.parent.name.capitalize())
    return names


def installed_product_names() -> list[str]:
    """Names to scan in VST3 folders (release bundle plus optional dev `… Local.vst3`)."""
    expanded: list[str] = []
    for name in plugin_names():
        expanded.append(name)
        expanded.append(f"{name} Local")
    return expanded


def find_bundles(build_dir: Path, only: Optional[set[str]] = None) -> list[Path]:
    """Built .vst3 bundles, one per plugin, preferring Release, then the newest build. A build dir that was
    configured both with and without AA_LOCAL_BUILD holds `X.vst3` and `X Local.vst3`: only the newest goes in."""
    found: dict[str, list[Path]] = {}
    for bundle in build_dir.glob("plugins/*/*_artefacts/*/VST3/*.vst3"):
        if bundle.is_dir():
            found.setdefault(bundle_product(bundle.name), []).append(bundle)
    result = []
    for product in sorted(found):
        if only and product not in only:
            continue
        candidates = sorted(found[product], key=lambda p: (p.parts[-3] != "Release", -bundle_mtime(p), str(p)))
        result.append(candidates[0])
    return sorted(result, key=lambda p: p.name)


def bundle_mtime(bundle: Path) -> float:
    """When the bundle was last built (its newest file)."""
    return max((f.stat().st_mtime for f in bundle.rglob("*") if f.is_file()), default=bundle.stat().st_mtime)


def bundle_vst3_stem(bundle_name: str) -> str:
    return bundle_name[: -len(".vst3")] if bundle_name.endswith(".vst3") else bundle_name


def bundle_product(bundle_name: str) -> str:
    stem = bundle_vst3_stem(bundle_name)
    if stem.endswith(" Local"):
        stem = stem[: -len(" Local")]
    return stem.replace(" ", "").lower()


def binary_of(bundle: Path) -> Optional[Path]:
    """The plugin binary inside a VST3 bundle (or the file itself for a legacy single-file plugin)."""
    if bundle.is_file():
        return bundle
    contents = bundle / "Contents"
    if not contents.is_dir():
        return None
    for folder in sorted(contents.iterdir()):
        if folder.is_dir() and (folder.name == "MacOS" or folder.name.startswith(("x86_64", "aarch64", "arm64", "i386"))):
            for item in sorted(folder.iterdir()):
                if item.is_file():
                    return item
    return None


@dataclass
class Stamp:
    version: str
    sha: str
    date: str

    def __str__(self) -> str:
        return f"{self.version} | {self.sha} | {self.date}"


def read_stamp(bundle: Path) -> Optional[Stamp]:
    """Build stamp embedded in the binary, or None for builds that predate it."""
    binary = binary_of(bundle)
    if binary is None:
        return None
    try:
        match = MARKER.search(binary.read_bytes())
    except OSError:
        return None
    if match is None:
        return None
    return Stamp(*(group.decode("utf-8", "replace") for group in match.groups()))


def repo_head() -> Optional[str]:
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=7", "HEAD"], capture_output=True,
                             text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


# ---------------------------------------------------------------- actions (all side effects go through here)

class Runner:
    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self.errors: list[str] = []

    def say(self, text: str = "") -> None:
        print(text, flush=True)

    def step(self, text: str) -> None:
        self.say(("[dry-run] " if self.dry_run else "") + text)

    def run(self, command: list[str], cwd: Optional[Path] = None, check: bool = True) -> int:
        self.step("$ " + " ".join(command))
        if self.dry_run:
            return 0
        result = subprocess.run(command, cwd=cwd)
        if check and result.returncode != 0:
            raise SystemExit(f"command failed ({result.returncode}): {' '.join(command)}")
        return result.returncode

    def remove(self, path: Path) -> bool:
        self.step(f"remove {path}")
        if self.dry_run:
            return True
        try:
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
            return True
        except OSError as error:
            hint = "re-run as administrator" if detect_os() == "windows" else f"try: sudo rm -rf '{path}'"
            self.say(f"  could not remove {path}: {error} ({hint})")
            self.errors.append(f"could not remove {path}")
            return False

    def copy_bundle(self, source: Path, destination: Path) -> None:
        self.step(f"copy {source} -> {destination}")
        if self.dry_run:
            return
        destination.parent.mkdir(parents=True, exist_ok=True)
        staging = destination.with_name(destination.name + ".aa-new")
        if staging.exists():
            shutil.rmtree(staging, ignore_errors=True)
        shutil.copytree(source, staging, symlinks=True)
        if destination.exists():
            shutil.rmtree(destination) if destination.is_dir() else destination.unlink()
        staging.rename(destination)


    def copy_file(self, source: Path, destination: Path) -> None:
        self.step(f"copy {source} -> {destination}")
        if self.dry_run:
            return
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    def write_text(self, path: Path, text: str) -> None:
        self.step(f"write {path}")
        if self.dry_run:
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


# ---------------------------------------------------------------- Max for Live devices + Live browser tag

ABLETON_TAG = "Creator|Animal Audio"  # the tag Live 12 gives the VST3s from their vendor name
FOLDER_INFO = "Ableton Folder Info"
FOLDER_XMP = "dc66a3fa-0fe1-5352-91cf-3ec237e9ee90.xmp"  # the name Live uses for a folder's tag sidecar
M4L_FOLDERS = {
    b"aaaa": ("Audio Effects", "Max Audio Effect"),
    b"iiii": ("Instruments", "Max Instrument"),
    b"mmmm": ("MIDI Effects", "Max MIDI Effect"),
}
XMP_NS = {
    "x": "adobe:ns:meta/",
    "rdf": "http://www.w3.org/1999/02/22-rdf-syntax-ns#",
    "dc": "http://purl.org/dc/elements/1.1/",
    "ablFR": "https://ns.ableton.com/xmp/fs-resources/1.0/",
    "xmp": "http://ns.adobe.com/xap/1.0/",
}
for _prefix, _uri in XMP_NS.items():
    ET.register_namespace(_prefix, _uri)


def m4l_devices(only: Optional[set[str]] = None) -> list[Path]:
    devices = sorted(ROOT.glob("plugins/*/m4l/*.amxd"))
    return [d for d in devices if not only or d.parent.parent.name.lower() in only or d.stem.lower() in only]


def live_library_cfgs(os_name: str) -> list[Path]:
    if os_name == "macos":
        base = Path.home() / "Library" / "Preferences" / "Ableton"
        return list(base.glob("Live */Library.cfg"))
    if os_name == "windows":
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")) / "Ableton"
        return list(base.glob("Live */Preferences/Library.cfg"))
    return []


def user_library(os_name: str, override: Optional[str] = None) -> Optional[Path]:
    """Live's User Library: from the newest Live's Library.cfg, else the default location."""
    if override:
        return Path(override).expanduser()
    cfgs = sorted(live_library_cfgs(os_name), key=lambda p: p.stat().st_mtime, reverse=True)
    for cfg in cfgs:
        text = cfg.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"<UserLibrary>.*?<ProjectPath Value=\"([^\"]+)\"", text, re.S)
        if match:
            return Path(match.group(1)) / "User Library"
    if os_name == "macos":
        return Path.home() / "Music" / "Ableton" / "User Library"
    if os_name == "windows":
        return Path.home() / "Documents" / "Ableton" / "User Library"
    return None


def m4l_folder(library: Path, device: Path) -> Path:
    category, subfolder = M4L_FOLDERS.get(device.read_bytes()[8:12], M4L_FOLDERS[b"aaaa"])
    return library / "Presets" / category / subfolder / "Animal Audio"


def tagged_sidecar(existing: Optional[str], names: Iterable[str], tag: str = ABLETON_TAG) -> str:
    """The folder's Live tag sidecar with `tag` on every file in `names`, keeping whatever else it holds
    (tags or colours the user added in Live)."""
    def q(prefix: str, name: str) -> str:
        return f"{{{XMP_NS[prefix]}}}{name}"

    root = None
    if existing:
        try:
            root = ET.fromstring(existing)
        except ET.ParseError:
            root = None
    if root is None:
        root = ET.Element(q("x", "xmpmeta"))
        desc = ET.SubElement(ET.SubElement(root, q("rdf", "RDF")), q("rdf", "Description"), {q("rdf", "about"): ""})
        ET.SubElement(desc, q("dc", "format")).text = "application/vnd.ableton.folder"
        ET.SubElement(desc, q("ablFR", "resource")).text = "folder"
        ET.SubElement(desc, q("xmp", "CreatorTool")).text = "Animal Audio"
    desc = root.find("rdf:RDF/rdf:Description", XMP_NS)
    items = desc.find("ablFR:items", XMP_NS)
    if items is None:
        items = ET.Element(q("ablFR", "items"))
        desc.insert(2, items)
    bag = items.find("rdf:Bag", XMP_NS)
    if bag is None:
        bag = ET.SubElement(items, q("rdf", "Bag"))

    entries = {}
    for li in bag.findall("rdf:li", XMP_NS):
        path = li.find("ablFR:filePath", XMP_NS)
        if path is not None and path.text:
            entries[path.text] = li
    for name in names:
        li = entries.get(name)
        if li is None:
            li = ET.SubElement(bag, q("rdf", "li"), {q("rdf", "parseType"): "Resource"})
            ET.SubElement(li, q("ablFR", "filePath")).text = name
        keywords = li.find("ablFR:keywords", XMP_NS)
        if keywords is None:
            keywords = ET.SubElement(li, q("ablFR", "keywords"))
        kbag = keywords.find("rdf:Bag", XMP_NS)
        if kbag is None:
            kbag = ET.SubElement(keywords, q("rdf", "Bag"))
        if tag not in [k.text for k in kbag.findall("rdf:li", XMP_NS)]:
            ET.SubElement(kbag, q("rdf", "li")).text = tag
    ET.indent(root, space="    ")
    xml = ET.tostring(root, encoding="unicode")
    # ElementTree declares every namespace on the root; put them where Live writes them.
    xml = re.sub(r"^<x:xmpmeta[^>]*>", '<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="XMP Core 6.0.0">', xml)
    xml = xml.replace("<rdf:RDF>", f'<rdf:RDF xmlns:rdf="{XMP_NS["rdf"]}">', 1)
    xml = re.sub(r'<rdf:Description rdf:about="">',
                 '<rdf:Description rdf:about=""\n                xmlns:dc="{dc}"\n                xmlns:ablFR="{ablFR}"\n'
                 '                xmlns:xmp="{xmp}">'.format(**XMP_NS), xml, count=1)
    return xml + "\n"


def install_m4l(args, runner: Runner, os_name: str) -> int:
    only = {n.strip().lower() for n in args.only.split(",")} if args.only else None
    devices = m4l_devices(only)
    if not devices:
        return 0
    library = user_library(os_name, args.user_library)
    if library is None:
        runner.say("No Ableton User Library on this OS; skipping the Max for Live devices.")
        return 0
    if not library.is_dir() and not args.user_library:
        runner.say(f"warning: Ableton User Library not found at {library}; skipping the Max for Live devices "
                   "(pass --user-library)")
        return 0
    runner.say(f"\nInstalling {len(devices)} Max for Live device(s) into {library}, tagged {ABLETON_TAG}")
    by_folder: dict[Path, list[str]] = {}
    for device in devices:
        folder = m4l_folder(library, device)
        runner.copy_file(device, folder / device.name)
        by_folder.setdefault(folder, []).append(device.name)
    for folder, names in by_folder.items():
        sidecar = folder / FOLDER_INFO / FOLDER_XMP
        existing = sidecar.read_text(encoding="utf-8", errors="replace") if sidecar.exists() else None
        runner.write_text(sidecar, tagged_sidecar(existing, names))
    return 1 if runner.errors else 0


def installed_m4l(library: Optional[Path]) -> list[Path]:
    if library is None:
        return []
    return [library / "Presets" / c / s / "Animal Audio" for c, s in M4L_FOLDERS.values()
            if (library / "Presets" / c / s / "Animal Audio").is_dir()]


# ---------------------------------------------------------------- prerequisites

def cmake_version() -> Optional[tuple[int, ...]]:
    try:
        out = subprocess.run(["cmake", "--version"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", out)
    return tuple(int(part) for part in match.groups(default="0")) if match else None


def windows_build_tools() -> bool:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.exists():
        return False
    out = subprocess.run([str(vswhere), "-products", "*", "-requires",
                          "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                         capture_output=True, text=True)
    return bool(out.stdout.strip())


LINUX_PKGS = ["x11", "xrandr", "xinerama", "xcursor", "freetype2", "fontconfig", "alsa"]
APT_PACKAGES = ["build-essential", "cmake", "git", "ninja-build", "pkg-config", "libasound2-dev", "libfreetype-dev",
                "libfontconfig1-dev", "libx11-dev", "libxrandr-dev", "libxinerama-dev", "libxcursor-dev",
                "libxext-dev", "libgl1-mesa-dev", "libcurl4-openssl-dev"]


def check_prereqs(os_name: str) -> list[tuple[str, bool, str]]:
    """(name, ok, how to fix) for each prerequisite. Ninja is optional."""
    rows: list[tuple[str, bool, str]] = []
    version = cmake_version()
    rows.append((f"cmake >= {MIN_CMAKE[0]}.{MIN_CMAKE[1]}" + (f" (found {'.'.join(map(str, version))})" if version else ""),
                 bool(version and version[:2] >= MIN_CMAKE), "install CMake from https://cmake.org/download/"))
    rows.append(("git", shutil.which("git") is not None, "install git"))
    rows.append(("ninja (optional, faster builds)", shutil.which("ninja") is not None, "install ninja"))
    if os_name == "macos":
        clt = subprocess.run(["xcode-select", "-p"], capture_output=True).returncode == 0
        rows.append(("Xcode Command Line Tools (clang)", clt, "xcode-select --install"))
    elif os_name == "windows":
        rows.append(("Visual Studio 2022 Build Tools (C++ workload)", windows_build_tools(),
                     "winget install Microsoft.VisualStudio.2022.BuildTools"))
    else:
        rows.append(("C++ compiler (c++/g++/clang++)", any(shutil.which(c) for c in ("c++", "g++", "clang++")),
                     "sudo apt-get install build-essential"))
        if shutil.which("pkg-config"):
            missing = [p for p in LINUX_PKGS if subprocess.run(["pkg-config", "--exists", p]).returncode != 0]
            rows.append(("JUCE system libraries" + (f" (missing: {', '.join(missing)})" if missing else ""),
                         not missing, "sudo apt-get install " + " ".join(APT_PACKAGES)))
        else:
            rows.append(("pkg-config (to check JUCE system libraries)", False, "sudo apt-get install pkg-config"))
    return rows


def install_prereqs(os_name: str, runner: Runner, assume_yes: bool) -> None:
    if os_name == "macos":
        if subprocess.run(["xcode-select", "-p"], capture_output=True).returncode != 0:
            runner.say("Installing the Xcode Command Line Tools opens a system dialog; accept it, wait for it to "
                       "finish, then re-run this script.")
            runner.run(["xcode-select", "--install"], check=False)
        if shutil.which("brew"):
            runner.run(["brew", "install", "cmake", "ninja", "git"])
        else:
            runner.say("Homebrew not found: install it from https://brew.sh, or install CMake/Ninja manually.")
    elif os_name == "windows":
        if not shutil.which("winget"):
            runner.say("winget not found: install CMake, Git, Ninja and Visual Studio 2022 Build Tools (C++) manually.")
            return
        runner.run(["winget", "install", "-e", "--id", "Kitware.CMake"], check=False)
        runner.run(["winget", "install", "-e", "--id", "Git.Git"], check=False)
        runner.run(["winget", "install", "-e", "--id", "Ninja-build.Ninja"], check=False)
        runner.run(["winget", "install", "-e", "--id", "Microsoft.VisualStudio.2022.BuildTools", "--override",
                    "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"], check=False)
    else:
        if not shutil.which("apt-get"):
            runner.say("Only apt-based distros are automated. Install: cmake git ninja a C++ compiler pkg-config and "
                       "the X11/ALSA/freetype/fontconfig/GL development packages (see README).")
            return
        prefix = [] if is_admin() else ["sudo"]
        runner.run(prefix + ["apt-get", "update"])
        runner.run(prefix + ["apt-get", "install"] + (["-y"] if assume_yes else []) + APT_PACKAGES)


# ---------------------------------------------------------------- build

def build_all(runner: Runner, build_dir: Path, clean: bool, only: Optional[set[str]], universal: bool,
              os_name: str) -> None:
    if clean and build_dir.exists():
        resolved = build_dir.resolve()
        if ROOT not in resolved.parents:
            raise SystemExit(f"refusing to clean {resolved}: not inside the repository")
        runner.remove(build_dir)

    configure = ["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release", "-DAA_BUILD_TESTS=OFF",
                 "-DAA_LOCAL_BUILD=ON"]
    if shutil.which("ninja") and os_name != "windows":
        configure[1:1] = ["-G", "Ninja"]
    if only:
        configure.append("-DAA_PLUGINS=" + ";".join(sorted(only)))
    if universal and os_name == "macos":
        configure.append("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
    runner.say("== configure (Release, every plugin under plugins/)")
    runner.run(configure)
    runner.say("== build")
    runner.run(["cmake", "--build", str(build_dir), "--config", "Release", "--parallel"])


# ---------------------------------------------------------------- install / uninstall / list

def target_dirs(args, os_name: str) -> tuple[Path, list[Path]]:
    """(folder to install into, other standard folders where copies would be stale)."""
    dirs = standard_dirs(os_name)
    if args.prefix:
        return Path(args.prefix).expanduser(), []
    scope = "system" if args.system else "user" if args.user else default_scope(os_name)
    chosen = dirs[scope][0]
    others = [d for s, lst in dirs.items() for d in lst if d != chosen]
    return chosen, others


def installed_copies(directories: Iterable[Path], names: Iterable[str]) -> list[Path]:
    wanted = {f"{n}.vst3" for n in names}
    found = []
    for directory in directories:
        if directory.is_dir():
            found += [child for child in sorted(directory.iterdir()) if child.name in wanted]
    return found


def prepare_for_host(runner: Runner, bundle: Path, os_name: str) -> None:
    """macOS refuses unsigned or quarantined bundles; a local ad-hoc signature is enough for a locally built plugin."""
    if os_name != "macos":
        return
    runner.run(["xattr", "-dr", "com.apple.quarantine", str(bundle)], check=False)
    runner.run(["codesign", "--force", "--deep", "--sign", "-", str(bundle)], check=False)


def do_install(args, runner: Runner, os_name: str) -> int:
    build_dir = Path(args.build_dir)
    only = {n.lower() for n in args.only.split(",")} if args.only else None
    bundles = find_bundles(build_dir, only)
    known = plugin_names()
    if not bundles:
        runner.say(f"No built .vst3 bundles under {build_dir}. Build first: python3 scripts/install_plugins.py --build")
        return 1
    built = {bundle_product(b.name).lower() for b in bundles}
    for name in known:
        if name.lower() not in built and (not only or name.lower() in only):
            runner.say(f"warning: plugin '{name}' is in plugins/ but has no built bundle in {build_dir}")

    destination_dir, others = target_dirs(args, os_name)
    runner.say(f"Installing {len(bundles)} plugin(s) into {destination_dir}")
    if os_name == "windows" and not args.prefix and destination_dir == standard_dirs(os_name)["user"][0] \
            and not args.user:
        runner.say("note: not running as administrator, so the per-user VST3 folder is used "
                   "(hosts scan it too). Use an elevated terminal with --system for %COMMONPROGRAMFILES%\\VST3.")

    for bundle in bundles:
        new_stamp = read_stamp(bundle)
        old = destination_dir / bundle.name
        if old.exists():
            old_stamp = read_stamp(old)
            runner.say(f"- {bundle.name}: replacing existing copy ({old_stamp or 'no build stamp: older build'})")
        else:
            runner.say(f"- {bundle.name}: new install")
        runner.copy_bundle(bundle, old)
        prepare_for_host(runner, old, os_name)
        if not args.keep_stale:
            for stale in installed_copies(others, [bundle_vst3_stem(bundle.name)]):
                runner.say(f"  stale copy in another VST3 folder ({read_stamp(stale) or 'no build stamp'}): {stale}")
                runner.remove(stale)
        else:
            for stale in installed_copies(others, [bundle_vst3_stem(bundle.name)]):
                runner.say(f"  warning: another copy exists at {stale}; a host may load that one instead")

    report(runner, [destination_dir / b.name for b in bundles], dry_run=runner.dry_run)
    return 1 if runner.errors else 0


def report(runner: Runner, paths: list[Path], dry_run: bool) -> None:
    runner.say()
    runner.say("Installed plugins" + (" (dry run: nothing was written)" if dry_run else "") + ":")
    head = repo_head()
    for path in paths:
        stamp = read_stamp(path) if path.exists() else None
        runner.say(f"  {path}")
        runner.say(f"      build stamp: {stamp if stamp else '(not readable)' if path.exists() else '(would be installed)'}")
        if stamp and head and not stamp.sha.startswith(head) and not dry_run:
            runner.say(f"      note: built from {stamp.sha}, but this checkout is at {head}; rebuild with --build --clean")


def do_uninstall(args, runner: Runner, os_name: str) -> int:
    dirs = [Path(args.prefix).expanduser()] if args.prefix else [d for lst in standard_dirs(os_name).values() for d in lst]
    names = installed_product_names()
    if getattr(args, "only", None):
        only = {p.strip().lower() for p in args.only.split(",") if p.strip()}
        names = [n for n in names if n.replace(" Local", "").lower() in only]
    copies = installed_copies(dirs, names)
    if not args.no_m4l and (not args.prefix or args.user_library):
        library = user_library(os_name, args.user_library)
        for folder in installed_m4l(library):
            for device in sorted(folder.glob("*.amxd")):
                if not getattr(args, "only", None) or device.stem.lower() in args.only.lower().split(","):
                    runner.say(f"- {device.name} (Max for Live, {folder})")
                    runner.remove(device)
    if not copies:
        runner.say("No Animal Audio plugins found in the VST3 folders: " + ", ".join(str(d) for d in dirs))
        return 1 if runner.errors else 0
    for copy in copies:
        runner.say(f"- {copy.name} ({read_stamp(copy) or 'no build stamp'})")
        runner.remove(copy)
    runner.say("\nRescan plug-ins in your host afterwards so it forgets them.")
    return 1 if runner.errors else 0


def do_list(args, runner: Runner, os_name: str) -> int:
    dirs = [Path(args.prefix).expanduser()] if args.prefix else [d for lst in standard_dirs(os_name).values() for d in lst]
    copies = installed_copies(dirs, installed_product_names())
    head = repo_head()
    runner.say(f"This checkout is at {head or 'unknown'}. Animal Audio plugins installed on this machine:")
    if not copies:
        runner.say("  (none)")
    for copy in copies:
        stamp = read_stamp(copy)
        mark = "" if (stamp and head and stamp.sha.startswith(head)) else "   <- not this checkout's commit"
        runner.say(f"  {copy}\n      build stamp: {stamp if stamp else 'none (built before build stamps existed)'}{mark}")
    if not args.prefix or args.user_library:
        devices = [d for f in installed_m4l(user_library(os_name, args.user_library)) for d in sorted(f.glob("*.amxd"))]
        if devices:
            runner.say(f"Max for Live devices (tag {ABLETON_TAG}):")
            for device in devices:
                runner.say(f"  {device}")
    if len({c.name for c in copies}) != len(copies):
        runner.say("\nwarning: the same plugin is installed in more than one folder; remove the stale one(s) "
                   "(re-run the install, which removes them).")
    return 0


HOST_INSTRUCTIONS = """
Next, in Ableton Live:
  1. Preferences > Plug-ins: make sure "Use VST3 Plug-In System Folders" is ON.
  2. Click "Rescan". If a plugin does not appear (or still looks old), hold Alt (Windows) / Option (macOS) while
     clicking Rescan for a full rescan. Quit and reopen Live if it still shows the old build.
  3. Find the plugins under Plug-Ins > VST3 (category: Animal Audio), drop one on an audio track and test it.
     Everything (VST3s and Max for Live devices) is also under the browser tag Creator > Animal Audio.
  4. Verify the build: hover the ANIMAL AUDIO badge in the plugin UI (or read the small text at the bottom of the
     left panel). It must match the build stamp printed above.
"""


# ---------------------------------------------------------------- main

def parse_args(argv: Optional[list[str]] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    action = p.add_mutually_exclusive_group()
    action.add_argument("--uninstall", action="store_true", help="remove every Animal Audio plugin from the VST3 folders")
    action.add_argument("--list", action="store_true", help="show installed copies and their build stamps")
    action.add_argument("--prereqs", action="store_true", help="only check build prerequisites")
    p.add_argument("--install-prereqs", action="store_true", help="install missing prerequisites (brew/winget/apt)")
    p.add_argument("--build", action="store_true", help="configure and build all plugins (Release) before installing")
    p.add_argument("--clean", action="store_true", help="with --build: delete the build directory first")
    p.add_argument("--universal", action="store_true", help="macOS: build arm64 + x86_64 (for Intel / Rosetta hosts)")
    p.add_argument("--only", help="comma-separated plugin names (default: all under plugins/)")
    p.add_argument("--build-dir", default=str(ROOT / "build"))
    scope = p.add_mutually_exclusive_group()
    scope.add_argument("--user", action="store_true", help="install into the per-user VST3 folder")
    scope.add_argument("--system", action="store_true", help="install into the machine-wide VST3 folder (needs admin)")
    p.add_argument("--prefix", help="install into / uninstall from this folder instead of the OS folders")
    p.add_argument("--keep-stale", action="store_true", help="do not remove copies from the other VST3 folders")
    p.add_argument("--yes", "-y", action="store_true", help="do not ask questions")
    p.add_argument("--dry-run", action="store_true", help="print what would happen, change nothing")
    p.add_argument("--user-library", help="Ableton User Library for the Max for Live devices (default: from Live's prefs)")
    m4l = p.add_mutually_exclusive_group()
    m4l.add_argument("--no-m4l", action="store_true", help="skip the Max for Live devices")
    m4l.add_argument("--m4l-only", action="store_true", help="only install the Max for Live devices (no VST3s)")
    return p.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    args = parse_args(argv)
    os_name = detect_os()
    runner = Runner(args.dry_run)

    if args.prereqs or args.install_prereqs:
        if args.install_prereqs:
            install_prereqs(os_name, runner, args.yes)
        rows = check_prereqs(os_name)
        failed = False
        for name, ok, fix in rows:
            optional = "optional" in name
            runner.say(f"  [{'ok' if ok else ('--' if optional else 'MISSING')}] {name}" + ("" if ok else f"   -> {fix}"))
            failed |= not ok and not optional
        if failed or not args.build:
            return 1 if failed else 0

    if args.list:
        return do_list(args, runner, os_name)
    if args.uninstall:
        return do_uninstall(args, runner, os_name)

    if args.m4l_only:
        return install_m4l(args, runner, os_name)
    if args.build:
        only = {n.lower() for n in args.only.split(",")} if args.only else None
        build_all(runner, Path(args.build_dir), args.clean, only, args.universal, os_name)
    status = do_install(args, runner, os_name)
    if not args.no_m4l and (not args.prefix or args.user_library):
        status = max(status, install_m4l(args, runner, os_name))
    if status == 0:
        runner.say(HOST_INSTRUCTIONS)
    return status


if __name__ == "__main__":
    sys.exit(main())
