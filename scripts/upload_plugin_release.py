#!/usr/bin/env python3
"""Upload a validated plugin release to Supabase Storage and publish it, listing included.

    python3 scripts/upload_plugin_release.py --manifest dist/releases/duck/1.0.0/manifest-mac.json

In order, so the Software Center never sees a release without its listing:
  1. the installer -> plugin-releases/<slug>/<version>/<file>
  2. the UI screenshot (docs/images/<slug>-ui.png) -> plugin-releases/<slug>/<version>/<Product>-ui.png
  3. public.plugin_catalog upsert from plugins/<slug>/catalog.json (name, copy, bundle, colours, screenshot)
  4. public.plugin_access row with catalog.json `initialAccess` (default admin only) if the plugin has none;
     an existing row is never changed, so availability set by an admin stays
  5. the active public.plugin_releases row for (plugin_slug, platform): updated in place, inserted on first release

New plugins need no hand-written SQL and no change in earth-audio.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from lib.plugin_release import PluginCatalog, get_plugin, load_catalog, screenshot_path  # noqa: E402

DEFAULT_WEB_REPO = Path(os.environ.get("AA_WEB_REPO", str(ROOT.parent / "earth-audio")))
# Pinned like earth-audio's devDependency: CI has no node_modules there, so a bare `npx supabase` pulls the
# latest major (3.x requires --yes for every remote write and broke publishing).
SUPABASE = ["npx", "supabase@^2.121.0"]


@dataclass
class Manifest:
    plugin_slug: str
    version: str
    platform: str
    file_name: str
    storage_path: str
    sha256: str
    byte_size: int

    @staticmethod
    def load(path: Path) -> "Manifest":
        data = json.loads(path.read_text(encoding="utf-8"))
        return Manifest(**data)

    def check(self) -> None:
        """Values are interpolated into SQL, so only accept the shapes the release pipeline produces."""
        rules = {
            "plugin_slug": r"[a-z0-9]+",
            "version": r"[0-9]+\.[0-9]+\.[0-9]+",
            "platform": r"mac|win",
            "file_name": r"[A-Za-z0-9._-]+",
            "storage_path": r"[a-z0-9]+/[0-9]+\.[0-9]+\.[0-9]+/[A-Za-z0-9._-]+",
            "sha256": r"[0-9a-fA-F]{64}",
        }
        for field, pattern in rules.items():
            value = getattr(self, field)
            if not isinstance(value, str) or not re.fullmatch(pattern, value):
                raise SystemExit(f"manifest {field} {value!r} is not a valid release value")
        if not isinstance(self.byte_size, int) or self.byte_size <= 0:
            raise SystemExit(f"manifest byte_size {self.byte_size!r} must be a positive integer")


def resolve_executable(name: str) -> str:
    # On Windows, npx is npx.cmd; CreateProcess does not apply PATHEXT to bare names.
    return shutil.which(name) or name


def redact(cmd: list[str]) -> list[str]:
    out = list(cmd)
    for i, arg in enumerate(out[:-1]):
        if arg in ("--token", "--password"):
            out[i + 1] = "***"
    return out


def run(cmd: list[str], *, cwd: Path, dry_run: bool) -> None:
    print("$ " + " ".join(redact(cmd)))
    if dry_run:
        return
    subprocess.run([resolve_executable(cmd[0]), *cmd[1:]], cwd=cwd, check=True)


def ensure_supabase_linked(web_repo: Path, *, dry_run: bool) -> None:
    token = os.environ.get("AA_SUPABASE_ACCESS_TOKEN") or os.environ.get("SUPABASE_ACCESS_TOKEN")
    if not token or dry_run:
        return
    ref = os.environ.get("AA_SUPABASE_PROJECT_REF", "huivhdradgroykfvtclx")
    run([*SUPABASE, "login", "--token", token], cwd=web_repo, dry_run=False)
    link = [*SUPABASE, "link", "--project-ref", ref, "--yes"]
    db_password = os.environ.get("AA_SUPABASE_DB_PASSWORD")
    if db_password:
        link.extend(["--password", db_password])
    run(link, cwd=web_repo, dry_run=False)


def sql_publish(m: Manifest) -> str:
    m.check()
    return (
        "with updated as ("
        "update public.plugin_releases "
        f"set version = '{m.version}', storage_path = '{m.storage_path}', file_name = '{m.file_name}', "
        f"sha256 = '{m.sha256}', byte_size = {m.byte_size}, published_at = now() "
        f"where plugin_slug = '{m.plugin_slug}' and platform = '{m.platform}' and is_active = true "
        "returning id) "
        "insert into public.plugin_releases "
        "(plugin_slug, version, platform, storage_path, file_name, sha256, byte_size, is_active) "
        f"select '{m.plugin_slug}', '{m.version}', '{m.platform}', '{m.storage_path}', '{m.file_name}', "
        f"'{m.sha256}', {m.byte_size}, true "
        "where not exists (select 1 from updated);"
    )


def sql_text(value: str) -> str:
    """A SQL string literal (standard_conforming_strings: only the quote needs escaping)."""
    if "\x00" in value:
        raise SystemExit("listing text contains a NUL character")
    return "'" + value.replace("'", "''") + "'"


def sql_array(values: tuple[str, ...], cast: str) -> str:
    return "array[" + ", ".join(sql_text(value) for value in values) + f"]::{cast}"


def sql_catalog(slug: str, bundle_name: str, catalog: PluginCatalog, screenshot: str | None) -> str:
    """Upsert the plugin's Software Center listing. A release without a screenshot keeps the previous one."""
    columns = (
        "plugin_slug, code, name, bundle_name, category, blurb, long_description, highlights, "
        "accent_color, surface_color, screenshot_path"
    )
    values = ", ".join(
        [
            sql_text(slug),
            sql_text(catalog.code),
            sql_text(catalog.name),
            sql_text(bundle_name),
            sql_text(catalog.category),
            sql_text(catalog.blurb),
            sql_text(catalog.long_description),
            sql_array(catalog.highlights, "text[]"),
            sql_text(catalog.accent),
            sql_text(catalog.surface),
            sql_text(screenshot) if screenshot else "null",
        ]
    )
    updates = ", ".join(
        f"{column} = excluded.{column}"
        for column in (
            "code", "name", "bundle_name", "category", "blurb", "long_description", "highlights",
            "accent_color", "surface_color",
        )
    )
    return (
        f"insert into public.plugin_catalog ({columns}) values ({values}) "
        f"on conflict (plugin_slug) do update set {updates}, "
        "screenshot_path = coalesce(excluded.screenshot_path, public.plugin_catalog.screenshot_path);"
    )


def sql_initial_access(slug: str, catalog: PluginCatalog) -> str:
    """First release only: who can see the plugin. Never overwrites availability an admin has set."""
    return (
        "insert into public.plugin_access (plugin_slug, roles) "
        f"values ({sql_text(slug)}, {sql_array(catalog.initial_access, 'public.app_role[]')}) "
        "on conflict (plugin_slug) do nothing;"
    )


def upload(local: Path, storage_path: str, content_type: str, *, web_repo: Path, dry_run: bool) -> None:
    storage_dst = f"ss:///plugin-releases/{storage_path}"
    # `storage cp` cannot overwrite (409 KeyAlreadyExists), and re-running a release for the same version
    # must work (rebuilt/re-signed artifact). `storage rm` exits 0 when the object does not exist.
    run(
        [*SUPABASE, "storage", "rm", "--experimental", "--linked", "--yes", storage_dst],
        cwd=web_repo,
        dry_run=dry_run,
    )
    run(
        [
            *SUPABASE,
            "storage",
            "cp",
            "--experimental",
            "--linked",
            "--content-type",
            content_type,
            "--yes",
            str(local),
            storage_dst,
        ],
        cwd=web_repo,
        dry_run=dry_run,
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--manifest", type=Path, required=True, help="JSON from validate_plugin_release.py")
    p.add_argument("--zip", type=Path, help="Artifact path (default: dist/releases/<slug>/<version>/<file>)")
    p.add_argument("--web-repo", type=Path, default=None, help="earth-audio checkout (for `supabase link`)")
    p.add_argument("--dry-run", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    web_repo = (args.web_repo or DEFAULT_WEB_REPO).expanduser().resolve()
    if not (web_repo / "supabase").is_dir():
        raise SystemExit(f"earth-audio not found at {web_repo} (set AA_WEB_REPO)")

    manifest = Manifest.load(args.manifest.expanduser().resolve())
    if not manifest.sha256:
        raise SystemExit("manifest sha256 is empty; run validate_plugin_release.py --write-manifest first")
    manifest.check()
    try:
        plugin = get_plugin(manifest.plugin_slug)
        catalog = load_catalog(manifest.plugin_slug)
    except ValueError as error:
        raise SystemExit(str(error)) from error

    ensure_supabase_linked(web_repo, dry_run=args.dry_run)

    artifact_path = (
        args.zip.expanduser().resolve()
        if args.zip
        else ROOT / "dist" / "releases" / manifest.plugin_slug / manifest.version / manifest.file_name
    )
    if not artifact_path.is_file():
        raise SystemExit(f"artifact not found: {artifact_path}")

    content_type = "application/zip"
    if artifact_path.suffix == ".pkg":
        content_type = "application/octet-stream"
    upload(artifact_path, manifest.storage_path, content_type, web_repo=web_repo, dry_run=args.dry_run)

    screenshot = screenshot_path(manifest.plugin_slug)
    screenshot_storage = None
    if screenshot.is_file():
        screenshot_storage = f"{manifest.plugin_slug}/{manifest.version}/{plugin.product_name}-ui.png"
        upload(screenshot, screenshot_storage, "image/png", web_repo=web_repo, dry_run=args.dry_run)
    else:
        print(f"warning: no {screenshot.relative_to(ROOT)}; the listing keeps its previous screenshot", file=sys.stderr)

    for query in (
        sql_catalog(manifest.plugin_slug, plugin.bundle_name, catalog, screenshot_storage),
        sql_initial_access(manifest.plugin_slug, catalog),
        sql_publish(manifest),
    ):
        run([*SUPABASE, "db", "query", "--linked", "--yes", query], cwd=web_repo, dry_run=args.dry_run)

    print(f"\nUploaded {manifest.file_name} -> plugin-releases/{manifest.storage_path}")
    print(f"Listed {catalog.name} in plugin_catalog ({plugin.bundle_name}"
          f"{', screenshot ' + screenshot_storage if screenshot_storage else ''})")
    print(f"Published plugin_releases ({manifest.plugin_slug}, {manifest.platform}): "
          f"sha256={manifest.sha256}, byte_size={manifest.byte_size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
