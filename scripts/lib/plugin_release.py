"""Per-plugin release metadata (semver, paths, Supabase slug, store listing).

Source of truth for product version: plugins/<slug>/VERSION (semver x.y.z).
CMake target and VST3 bundle name come from plugins/<slug>/CMakeLists.txt.
Store listing (Software Center name, copy, colours, first-release access): plugins/<slug>/catalog.json.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PLUGINS_DIR = ROOT / "plugins"
_SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
_CMAKE_TARGET = re.compile(r"aa_add_plugin\s*\(\s*(\w+)", re.MULTILINE)
_PRODUCT_NAME = re.compile(r'PRODUCT_NAME\s+"([^"]+)"')
_PASCAL_WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


@dataclass(frozen=True)
class PluginReleaseInfo:
    slug: str
    cmake_target: str
    product_name: str
    version: str

    @property
    def bundle_name(self) -> str:
        return f"{self.product_name}.vst3"

    @property
    def display_name(self) -> str:
        """Human-facing name for installers: PascalCase product split into words ("HoneyBadger" -> "Honey Badger")."""
        return " ".join(_PASCAL_WORD.findall(self.product_name)) or self.product_name

    @property
    def eula_file_name(self) -> str:
        return f"Animal-Audio-{self.product_name}-EULA.txt"

    @property
    def mac_pkg_identifier(self) -> str:
        return f"com.animalaudio.{self.slug}.vst3"

    @property
    def github_tag(self) -> str:
        return f"{self.slug}-v{self.version}"

    def release_root(self, repo_root: Path | None = None) -> Path:
        base = repo_root or ROOT
        return base / "dist" / "releases" / self.slug / self.version

    def mac_pkg_name(self) -> str:
        return f"{self.product_name}-{self.version}-mac-installer.pkg"

    def mac_zip_name(self) -> str:
        return f"{self.product_name}-{self.version}-mac-vst3.zip"

    def win_zip_name(self) -> str:
        return f"{self.product_name}-{self.version}-win-vst3.zip"

    def storage_path(self, file_name: str, version: str | None = None) -> str:
        return f"{self.slug}/{version or self.version}/{file_name}"

    def bundle_build_rel(self, config: str = "Release") -> str:
        return f"plugins/{self.slug}/{self.cmake_target}_artefacts/{config}/VST3/{self.bundle_name}"


def _read_version(version_file: Path) -> str:
    if not version_file.is_file():
        raise ValueError(f"missing VERSION file: {version_file}")
    version = version_file.read_text(encoding="utf-8").strip()
    if not _SEMVER.match(version):
        raise ValueError(f"invalid semver in {version_file}: {version!r} (expected x.y.z)")
    return version


def _parse_cmake(cmake_path: Path) -> tuple[str, str]:
    text = cmake_path.read_text(encoding="utf-8")
    target_match = _CMAKE_TARGET.search(text)
    product_match = _PRODUCT_NAME.search(text)
    if not target_match or not product_match:
        raise ValueError(f"could not parse aa_add_plugin / PRODUCT_NAME in {cmake_path}")
    return target_match.group(1), product_match.group(1)


def list_plugin_slugs(*, repo_root: Path | None = None) -> list[str]:
    plugins_dir = (repo_root / "plugins") if repo_root else PLUGINS_DIR
    slugs: list[str] = []
    if not plugins_dir.is_dir():
        return slugs
    for path in sorted(plugins_dir.iterdir()):
        if path.is_dir() and (path / "CMakeLists.txt").is_file():
            slugs.append(path.name)
    return slugs


def get_plugin(slug: str, *, repo_root: Path | None = None) -> PluginReleaseInfo:
    base = repo_root or ROOT
    plugin_dir = base / "plugins" / slug
    if not plugin_dir.is_dir():
        raise ValueError(f"unknown plugin slug: {slug}")
    cmake = plugin_dir / "CMakeLists.txt"
    version = _read_version(plugin_dir / "VERSION")
    target, product = _parse_cmake(cmake)
    return PluginReleaseInfo(slug=slug, cmake_target=target, product_name=product, version=version)


def read_plugin_version(slug: str, *, repo_root: Path | None = None) -> str:
    return get_plugin(slug, repo_root=repo_root).version


# ---------------------------------------------------------------- store listing (catalog.json)

ACCESS_ROLES = ("member", "vip", "super-vip", "admin")
_HEX = re.compile(r"^#[0-9a-f]{6}$")
_CODE = re.compile(r"^AA-[0-9]{3}$")


@dataclass(frozen=True)
class PluginCatalog:
    """What the Software Center shows for a plugin (`public.plugin_catalog`), from plugins/<slug>/catalog.json."""

    code: str
    name: str
    category: str
    blurb: str
    long_description: str
    highlights: tuple[str, ...]
    accent: str
    surface: str
    initial_access: tuple[str, ...]


def catalog_path(slug: str, *, repo_root: Path | None = None) -> Path:
    return (repo_root or ROOT) / "plugins" / slug / "catalog.json"


def screenshot_path(slug: str, *, repo_root: Path | None = None) -> Path:
    """The headless UI render the plugin's tests write (README image); uploaded next to each release."""
    return (repo_root or ROOT) / "docs" / "images" / f"{slug}-ui.png"


def parse_catalog(data: object, source: str = "catalog.json") -> PluginCatalog:
    """Validates a catalog.json document. Raises ValueError listing every problem."""
    problems: list[str] = []
    if not isinstance(data, dict):
        raise ValueError(f"{source}: expected a JSON object")

    def text(key: str, limit: int) -> str:
        value = data.get(key)
        if not isinstance(value, str) or not value.strip():
            problems.append(f"{key} must be a non-empty string")
            return ""
        if len(value) > limit:
            problems.append(f"{key} is longer than {limit} characters")
        if "\x00" in value:
            problems.append(f"{key} contains a NUL character")
        return value.strip()

    code = text("code", 16)
    if code and not _CODE.match(code):
        problems.append(f"code {code!r} must look like AA-004")
    name = text("name", 40)
    category = text("category", 40)
    blurb = text("blurb", 200)
    long_description = text("longDescription", 600)

    highlights = data.get("highlights")
    if not isinstance(highlights, list) or not 1 <= len(highlights) <= 6 or not all(
        isinstance(item, str) and item.strip() and len(item) <= 60 for item in highlights
    ):
        problems.append("highlights must be 1-6 non-empty strings of up to 60 characters")
        highlights = []

    colours = {}
    for key in ("accent", "surface"):
        value = data.get(key)
        if not isinstance(value, str) or not _HEX.match(value):
            problems.append(f"{key} must be a lowercase hex colour like #52e5f2")
        colours[key] = value if isinstance(value, str) else ""

    access = data.get("initialAccess", ["admin"])
    if not isinstance(access, list) or not access or not all(role in ACCESS_ROLES for role in access):
        problems.append(f"initialAccess must be a non-empty list of {', '.join(ACCESS_ROLES)}")
        access = ["admin"]

    unknown = set(data) - {
        "code", "name", "category", "blurb", "longDescription", "highlights", "accent", "surface", "initialAccess",
    }
    if unknown:
        problems.append(f"unknown keys: {', '.join(sorted(unknown))}")
    if problems:
        raise ValueError(f"{source}: " + "; ".join(problems))
    return PluginCatalog(
        code=code,
        name=name,
        category=category,
        blurb=blurb,
        long_description=long_description,
        highlights=tuple(item.strip() for item in highlights),
        accent=colours["accent"],
        surface=colours["surface"],
        initial_access=tuple(dict.fromkeys(access)),
    )


def load_catalog(slug: str, *, repo_root: Path | None = None) -> PluginCatalog:
    path = catalog_path(slug, repo_root=repo_root)
    if not path.is_file():
        raise ValueError(f"missing {path.relative_to(repo_root or ROOT)}: every plugin needs a store listing")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"{path}: invalid JSON ({error})") from error
    return parse_catalog(data, str(path.relative_to(repo_root or ROOT)))
