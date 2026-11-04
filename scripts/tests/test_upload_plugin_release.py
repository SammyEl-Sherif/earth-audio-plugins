"""Tests for scripts/upload_plugin_release.py (dry run / SQL generation only; no network)."""
from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import upload_plugin_release  # noqa: E402
from lib.plugin_release import PluginCatalog, list_plugin_slugs, load_catalog, parse_catalog  # noqa: E402


def manifest(**overrides: object) -> upload_plugin_release.Manifest:
    base = upload_plugin_release.Manifest(
        plugin_slug="honeybadger",
        version="0.1.0",
        platform="mac",
        file_name="HoneyBadger-0.1.0-mac-installer.pkg",
        storage_path="honeybadger/0.1.0/HoneyBadger-0.1.0-mac-installer.pkg",
        sha256="a" * 64,
        byte_size=4_194_304,
    )
    return replace(base, **overrides)


class UploadPluginReleaseTests(unittest.TestCase):
    def test_existing_object_is_removed_before_copy_so_reruns_can_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "web" / "supabase").mkdir(parents=True)
            artifact = tmp_path / "Duck-1.0.1-mac-installer.pkg"
            artifact.write_bytes(b"pkg")
            manifest_path = tmp_path / "manifest-mac.json"
            manifest_path.write_text(
                json.dumps(
                    {
                        "plugin_slug": "duck",
                        "version": "1.0.1",
                        "platform": "mac",
                        "file_name": artifact.name,
                        "storage_path": f"duck/1.0.1/{artifact.name}",
                        "sha256": "ab" * 32,
                        "byte_size": 3,
                    }
                ),
                encoding="utf-8",
            )

            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = upload_plugin_release.main(
                    [
                        "--manifest",
                        str(manifest_path),
                        "--zip",
                        str(artifact),
                        "--web-repo",
                        str(tmp_path / "web"),
                        "--dry-run",
                    ]
                )

        self.assertEqual(code, 0)
        lines = out.getvalue().splitlines()
        commands = [line for line in lines if line.startswith("$ npx supabase@^2.121.0 storage")]
        dst = f"ss:///plugin-releases/duck/1.0.1/{artifact.name}"
        shot = "ss:///plugin-releases/duck/1.0.1/Duck-ui.png"
        self.assertEqual(len(commands), 4)
        self.assertIn(" storage rm ", commands[0])
        self.assertTrue(commands[0].endswith(dst))
        self.assertIn(" storage cp ", commands[1])
        self.assertTrue(commands[1].endswith(dst))
        self.assertTrue(commands[2].endswith(shot))
        self.assertIn("--content-type image/png", commands[3])
        self.assertTrue(commands[3].endswith(shot))

        # Listing and access go in before the release row, so the app never sees a release without them.
        queries = [line for line in lines if line.startswith("$ npx supabase@^2.121.0 db query")]
        self.assertEqual(len(queries), 3)
        self.assertIn("insert into public.plugin_catalog", queries[0])
        self.assertIn("'duck/1.0.1/Duck-ui.png'", queries[0])
        self.assertIn("insert into public.plugin_access", queries[1])
        self.assertIn("public.plugin_releases", queries[2])


class PublishSqlTests(unittest.TestCase):
    def test_updates_the_active_row_and_inserts_on_first_release(self) -> None:
        sql = upload_plugin_release.sql_publish(manifest())
        self.assertIn("update public.plugin_releases", sql)
        self.assertIn("where plugin_slug = 'honeybadger' and platform = 'mac' and is_active = true", sql)
        self.assertIn("insert into public.plugin_releases", sql)
        self.assertIn("where not exists (select 1 from updated)", sql)
        self.assertIn("'honeybadger/0.1.0/HoneyBadger-0.1.0-mac-installer.pkg'", sql)
        self.assertTrue(sql.rstrip().endswith(";"))
        self.assertEqual(sql.count(";"), 1)

    def test_rejects_values_that_are_not_pipeline_output(self) -> None:
        bad = [
            {"plugin_slug": "duck'; drop table x; --"},
            {"plugin_slug": "Duck"},
            {"version": "1.0"},
            {"platform": "linux"},
            {"file_name": "Duck 1.0.pkg"},
            {"storage_path": "../duck/1.0.0/x.pkg"},
            {"sha256": "abc"},
            {"byte_size": 0},
        ]
        for override in bad:
            with self.subTest(override=override), self.assertRaises(SystemExit):
                upload_plugin_release.sql_publish(manifest(**override))


def catalog(**overrides: object) -> PluginCatalog:
    base = PluginCatalog(
        code="AA-009",
        name="HONEY BADGER",
        category="Distortion",
        blurb="It doesn't care.",
        long_description="Bites through anything.",
        highlights=("Fearless", "Loud"),
        accent="#ffaa00",
        surface="#101010",
        initial_access=("admin",),
    )
    return replace(base, **overrides)


class ListingSqlTests(unittest.TestCase):
    def test_catalog_upsert_quotes_text_and_keeps_the_old_screenshot_when_none_is_given(self) -> None:
        sql = upload_plugin_release.sql_catalog("honeybadger", "HoneyBadger.vst3", catalog(), None)
        self.assertIn("insert into public.plugin_catalog", sql)
        self.assertIn("'It doesn''t care.'", sql)
        self.assertIn("array['Fearless', 'Loud']::text[]", sql)
        self.assertIn("'HoneyBadger.vst3'", sql)
        self.assertIn("on conflict (plugin_slug) do update", sql)
        self.assertIn("screenshot_path = coalesce(excluded.screenshot_path, public.plugin_catalog.screenshot_path)", sql)
        self.assertEqual(sql.count(";"), 1)

    def test_quotes_cannot_break_out_of_a_literal(self) -> None:
        sql = upload_plugin_release.sql_catalog(
            "honeybadger", "HoneyBadger.vst3", catalog(blurb="'); drop table x; --"), None
        )
        self.assertIn("''); drop table x; --'", sql)
        self.assertEqual(sql.count(";"), 3)  # two inside the literal, then the terminator

    def test_initial_access_never_overwrites_an_existing_row(self) -> None:
        sql = upload_plugin_release.sql_initial_access("honeybadger", catalog(initial_access=("admin", "vip")))
        self.assertIn("array['admin', 'vip']::public.app_role[]", sql)
        self.assertIn("on conflict (plugin_slug) do nothing", sql)


class CatalogFileTests(unittest.TestCase):
    def valid(self) -> dict:
        return {
            "code": "AA-009",
            "name": "HONEY BADGER",
            "category": "Distortion",
            "blurb": "b",
            "longDescription": "d",
            "highlights": ["x"],
            "accent": "#ffaa00",
            "surface": "#101010",
        }

    def test_every_plugin_has_a_valid_listing(self) -> None:
        for slug in list_plugin_slugs():
            with self.subTest(slug=slug):
                load_catalog(slug)

    def test_defaults_to_admin_only(self) -> None:
        self.assertEqual(parse_catalog(self.valid()).initial_access, ("admin",))

    def test_rejects_bad_listings(self) -> None:
        for change in (
            {"accent": "#FFAA00"},
            {"accent": "orange"},
            {"code": "4"},
            {"highlights": []},
            {"name": ""},
            {"initialAccess": ["everyone"]},
            {"extra": 1},
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                parse_catalog({**self.valid(), **change})


if __name__ == "__main__":
    unittest.main()
