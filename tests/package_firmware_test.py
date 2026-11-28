#!/usr/bin/env python3
"""Release-packaging contracts using synthetic images; never flash hardware.

The small images below exercise metadata/layout handling only. They are not ESP
application specimens and must never be distributed as firmware. Each fixture
uses an isolated temporary Git repository and ignored build/output directories.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
PACKAGE = Path(os.environ.get("DIVINER_PACKAGE_SCRIPT", REPOSITORY / "tools/package_firmware.py"))
METADATA = Path(os.environ.get("DIVINER_METADATA_SCRIPT", REPOSITORY / "tools/firmware_metadata.py"))
IDENTITY = Path(os.environ.get("DIVINER_IDENTITY_SCRIPT", REPOSITORY / "tools/source_identity.py"))
sys.path.insert(0, str(PACKAGE.parent))
CAPTURE = runpy.run_path(str(PACKAGE.with_name("source_provenance.py")))["capture"]


def sha(data): return hashlib.sha256(data).hexdigest()


def partition(kind, subtype, offset, size, name):
    return struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode(), 0)


class PackageContracts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="diviner-package-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.environment = "cyd"
        self.build = self.root/".pio/build/cyd"
        self.build.mkdir(parents=True)
        (self.root/"src").mkdir()
        (self.root/"src/main.cpp").write_text("// synthetic source identity fixture\n")
        (self.root/".gitignore").write_text(".pio/\ndelivery/\n")
        self.git("init", "-q")
        self.git("config", "user.name", "Diviner fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("add", ".")
        self.git("commit", "-qm", "Fixture baseline")
        self.head = self.git("rev-parse", "HEAD")
        self.tree = self.git("rev-parse", "HEAD^{tree}")
        table = (partition(1,2,0x9000,0x5000,"nvs") +
                 partition(1,0,0xe000,0x2000,"otadata") +
                 partition(0,0,0x10000,0x300000,"app0") +
                 partition(1,0x82,0x310000,0xf0000,"spiffs"))
        table += b"\xff"*(0xc00-len(table))
        self.files = [(0x1000,"bootloader.bin",b"\xe9"+b"boot"*63),
                      (0x8000,"partitions.bin",table),
                      (0xe000,"boot_app0.bin",b"\xff"*0x2000),
                      (0x10000,"firmware.bin",b"\xe9"+b"\x11"*(0x5000-1))]
        self.info = {"schema":1,"environment":"cyd",**CAPTURE(self.root),"platform":"espressif32",
                     "platform_version":"7.1.3","chip":"esp32","flash_size":"4MB",
                     "app_offset":0x10000,"images":[],"packages":[],"libraries":[]}
        for offset,name,data in self.files:
            (self.build/name).write_bytes(data)
            self.info["images"].append({"offset":offset,"source_file":str(self.build/name),
                "name":name,"bytes":len(data),"sha256":sha(data)})
        self.save_metadata()

    def git(self,*args):
        return subprocess.check_output(["git",*args],cwd=self.root,text=True,stderr=subprocess.DEVNULL).strip()

    def save_metadata(self):
        (self.build/"flash-layout.json").write_text(json.dumps(self.info))

    def package(self,*args):
        self.save_metadata()
        return subprocess.run([sys.executable,str(PACKAGE),self.environment,*args],cwd=self.root,
                              capture_output=True,text=True,timeout=20)

    def reject(self):
        result = self.package()
        self.assertNotEqual(result.returncode,0, "packager accepted an invalid candidate")

    def test_clean_bundle_uses_actual_offsets_and_hashes(self):
        result=self.package(); self.assertEqual(result.returncode,0,result.stderr)
        out=self.root/"delivery/cyd"
        manifest=json.loads((out/"manifest.json").read_text())
        self.assertEqual(manifest["source_sha"],self.head)
        self.assertEqual(manifest["source_tree"],self.tree)
        self.assertEqual(manifest["source_snapshot"],self.info["source_snapshot"])
        self.assertEqual(manifest["firmware_bytes"],0x5000)
        self.assertEqual(manifest["app_capacity"],0x300000)
        self.assertEqual(manifest["app_headroom_bytes"],0x2fb000)
        merged=(out/"cyd-full.bin").read_bytes()
        for offset,name,data in self.files:
            self.assertEqual(merged[offset:offset+len(data)],data,name)
        self.assertEqual(merged[0:0x1000],b"\xff"*0x1000)
        for line in (out/"SHA256SUMS.txt").read_text().splitlines():
            digest,name=line.split("  ",1)
            self.assertEqual(digest,sha((out/name).read_bytes()),name)
        self.assertFalse(manifest["flash_performed"])
        self.assertNotIn("source_file",json.dumps(manifest))

    def test_cardputer_uses_reported_app_and_bootloader_offsets(self):
        self.environment="cardputer"
        new_build=self.root/".pio/build/cardputer"
        self.build.rename(new_build);self.build=new_build
        self.info.update(environment="cardputer",chip="esp32s3",flash_size="16MB",app_offset=0x20000)
        for entry in self.info["images"]:
            entry["source_file"]=str(self.build/entry["name"])
        self.info["images"][0]["offset"]=0
        self.info["images"][-1]["offset"]=0x20000
        self.replace_table(partition(1,2,0x9000,0x5000,"nvs")+
                           partition(1,0,0xe000,0x2000,"otadata")+
                           partition(0,0,0x20000,0x300000,"diviner"))
        result=self.package();self.assertEqual(result.returncode,0,result.stderr)
        out=self.root/"delivery/cardputer"
        merged=(out/"cardputer-full.bin").read_bytes()
        self.assertEqual(merged[:len(self.files[0][2])],self.files[0][2])
        self.assertEqual(merged[0x20000:],self.files[-1][2])
        manifest=json.loads((out/"manifest.json").read_text())
        self.assertEqual(manifest["app_offset"],0x20000)
        self.assertEqual(manifest["app_headroom_bytes"],0x2fb000)

    def test_reject_changed_image(self):
        (self.build/"firmware.bin").write_bytes(b"different")
        self.reject()

    def test_reject_stale_head(self):
        self.info["source_sha"]="0"*40; self.reject()

    def test_reject_changed_tracked_source(self):
        (self.root/"src/main.cpp").write_text("// changed after build\n")
        self.reject()

    def test_reject_untracked_compiled_source(self):
        (self.root/"src/new_instrument.cpp").write_text("int new_instrument=17;\n")
        self.reject()

    def test_reject_wrong_source_tree(self):
        self.info["source_tree"]="0"*40; self.reject()

    def test_reject_wrong_source_snapshot(self):
        self.info["source_snapshot"]="0"*64; self.reject()

    def test_reject_wrong_environment(self):
        self.info["environment"]="cardputer"; self.reject()

    def test_reject_wrong_chip(self):
        self.info["chip"]="esp32s3"; self.reject()

    def test_reject_overlapping_images(self):
        self.info["images"][0]["offset"]=0x8000
        self.reject()

    def test_reject_app_exceeding_slot(self):
        self.replace_table(partition(0,0,0x10000,0x2000,"tiny"))
        self.reject()

    def test_reject_partition_overlapping_another_partition(self):
        self.replace_table(partition(0,0,0x10000,0x300000,"app0")+
                           partition(1,2,0x11000,0x5000,"nvs"))
        self.reject()

    def test_reject_partition_outside_declared_flash(self):
        self.info["flash_size"]="1MB"; self.reject()

    def test_reject_image_outside_declared_flash(self):
        name="stray.bin"; data=b"stray"
        (self.build/name).write_bytes(data)
        self.info["images"].append({"offset":0x410000,"source_file":str(self.build/name),
            "name":name,"bytes":len(data),"sha256":sha(data)})
        self.reject()

    def test_reject_duplicate_output_name(self):
        self.info["images"][0]["name"]="firmware.bin"
        self.reject()

    def test_reject_path_in_output_name(self):
        self.info["images"][0]["name"]="../unexpected.bin"
        self.reject()
        self.assertFalse((self.root/"delivery/unexpected.bin").exists())

    def test_nonempty_destination_is_refused_without_modification(self):
        out=self.root/"delivery/cyd"; out.mkdir(parents=True)
        (out/"obsolete.bin").write_bytes(b"previous source")
        (out/"firmware.elf").write_bytes(b"previous symbols")
        before={p.name:p.read_bytes() for p in out.iterdir()}
        self.reject()
        self.assertEqual({p.name:p.read_bytes() for p in out.iterdir()},before)

    def test_invalid_candidate_leaves_no_partial_new_bundle(self):
        self.info["images"][-1]["sha256"]="0"*64
        self.reject()
        self.assertFalse((self.root/"delivery/cyd").exists())

    def test_debug_files_are_verified(self):
        data=b"synthetic symbols"
        path=self.build/"firmware.elf";path.write_bytes(data)
        self.info["debug_files"]=[{"name":path.name,"source_file":str(path),
                                  "bytes":len(data),"sha256":sha(data)}]
        path.write_bytes(b"stale symbols")
        self.reject()

    def test_unrecorded_debug_files_are_excluded(self):
        (self.build/"firmware.elf").write_bytes(b"unrecorded old symbols")
        result=self.package();self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse((self.root/"delivery/cyd/firmware.elf").exists())

    def test_development_override_retains_dirty_identity(self):
        (self.root/"src/main.cpp").write_text("// local development change\n")
        self.info.update(CAPTURE(self.root))
        self.assertTrue(self.info["dirty"])
        self.reject()
        result=self.package("--allow-dirty");self.assertEqual(result.returncode,0,result.stderr)
        manifest=json.loads((self.root/"delivery/cyd/manifest.json").read_text())
        self.assertTrue(manifest["dirty"])

    def test_snapshot_detects_same_length_changes_in_dirty_source(self):
        path=self.root/"src/main.cpp"
        path.write_text("// edit one\n")
        first=CAPTURE(self.root)
        path.write_text("// edit two\n")
        second=CAPTURE(self.root)
        self.assertTrue(first["dirty"] and second["dirty"])
        self.assertEqual(first["source_sha"],second["source_sha"])
        self.assertNotEqual(first["source_snapshot"],second["source_snapshot"])

    def test_ignored_build_products_do_not_change_source_identity(self):
        first=CAPTURE(self.root)
        (self.build/"untracked-build-output").write_bytes(b"new ignored product")
        self.assertEqual(first,CAPTURE(self.root))

    def test_failed_repackage_leaves_previous_bundle_unchanged(self):
        out=self.root/"delivery/cyd"; out.mkdir(parents=True)
        (out/"manifest.json").write_text('{"previous":"qualified"}')
        (out/"firmware.bin").write_bytes(b"previous firmware")
        (out/"SHA256SUMS.txt").write_text("previous checksums")
        before={p.name:p.read_bytes() for p in out.iterdir()}
        self.info["images"][-1]["sha256"]="0"*64
        self.reject()
        self.assertEqual({p.name:p.read_bytes() for p in out.iterdir()},before)

    def replace_table(self,entries):
        data=entries+b"\xff"*(0xc00-len(entries))
        (self.build/"partitions.bin").write_bytes(data)
        self.info["images"][1].update(bytes=len(data),sha256=sha(data))

    def fake_env(self):
        test=self
        class Platform:
            name="espressif32"; version="7.1.3"; packages={}
        class Env:
            def subst(self,s):
                values={"$PROJECT_DIR":str(test.root),"$BUILD_DIR":str(test.build),
                    "$PROGNAME":"firmware","$ESP32_APP_OFFSET":"0x10000","$PIOENV":"cyd",
                    "$PROJECT_LIBDEPS_DIR":str(test.root/".pio/libdeps")}
                for key,value in values.items(): s=s.replace(key,value)
                return s
            def get(self,key,default=None):
                if key=="FLASH_EXTRA_IMAGES":
                    return [(off,str(test.build/name)) for off,name,_ in test.files[:-1]]
                return default
            def Append(self,**kwargs): pass
            def AddPostAction(self,*args): pass
            def PioPlatform(self): return Platform()
            def BoardConfig(self): return {"build.mcu":"esp32","upload.flash_size":"4MB"}
        return Env()

    def test_identity_marks_untracked_source_dirty(self):
        (self.root/"src/new_instrument.cpp").write_text("int new_instrument=17;\n")
        env=self.fake_env()
        runpy.run_path(str(IDENTITY),init_globals={"env":env,"Import":lambda _:None})
        header=(self.build/"diviner_build_identity_generated.h").read_text()
        self.assertIn('#define DIVINER_TREE_STATE "dirty"',header)

    def test_metadata_does_not_relabel_build_after_commit_changes(self):
        env=self.fake_env()
        runpy.run_path(str(IDENTITY),init_globals={"env":env,"Import":lambda _:None})
        (self.root/"src/main.cpp").write_text("// committed while compiler was running\n")
        self.git("add","src/main.cpp");self.git("commit","-qm","Concurrent source change")
        newer=self.git("rev-parse","HEAD")
        module=runpy.run_path(str(METADATA),init_globals={"env":env,"Import":lambda _:None})
        try:
            module["record"](None,None,env)
        except (RuntimeError,ValueError):
            return # rejecting the build is the strongest outcome
        metadata=json.loads((self.build/"flash-layout.json").read_text())
        self.assertFalse(metadata["source_sha"]==newer and not metadata["dirty"],
                         "post-build hook labelled old compiled identity as the new clean commit")

    def test_metadata_records_library_properties_dependencies(self):
        dependency=self.root/".pio/libdeps/cyd/TouchLibrary"
        dependency.mkdir(parents=True)
        (dependency/"library.properties").write_text("name=TouchLibrary\nversion=2.0.1\n")
        env=self.fake_env()
        runpy.run_path(str(IDENTITY),init_globals={"env":env,"Import":lambda _:None})
        module=runpy.run_path(str(METADATA),init_globals={"env":env,"Import":lambda _:None})
        module["record"](None,None,env)
        metadata=json.loads((self.build/"flash-layout.json").read_text())
        self.assertIn({"name":"TouchLibrary","version":"2.0.1"},metadata["libraries"])


if __name__=="__main__":
    unittest.main(verbosity=2)
