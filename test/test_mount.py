import os
import shutil

from helper import run
from conftest import root


@root
def test_mount(tmp_path):
    shutil.copy("good-bundle.raucb", tmp_path / "good-bundle.raucb")

    out, err, exitcode = run(f"rauc --conf=test.conf mount {tmp_path}/good-bundle.raucb")

    assert exitcode == 0
    assert "Mounted bundle at /mnt/rauc/bundle" in out

    assert os.path.exists("/mnt/rauc/bundle/manifest.raucm")
    assert os.path.exists("/mnt/rauc/bundle/rootfs.img")

    out, err, exitcode = run("umount /mnt/rauc/bundle")
    assert exitcode == 0
