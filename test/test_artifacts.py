import shutil
import tarfile
import json
from io import BytesIO
from pprint import pprint
from pathlib import Path
from contextlib import contextmanager

from conftest import root, have_json, Bundle
from helper import run, run_tree


pytestmark = [root, have_json]


def make_tarfile(path, contents):
    with tarfile.open(name=path, mode="w") as t:
        for filename, content in contents.items():
            f = tarfile.TarInfo(name=filename)
            c = BytesIO(content)
            f.size = len(content)
            t.addfile(f, c)


def get_info(path):
    out, err, exitcode = run(f"rauc --keyring openssl-ca/dev-ca.pem info --output-format=json-2 {path}")
    assert exitcode == 0

    info = json.loads(out)

    assert info
    assert info.get("bundle", {}).get("format") == "verity"

    return info


class RepoStatus(dict):
    @property
    def artifacts(self):
        result = {}

        for artifact in self.get("artifacts", []):
            for instance in artifact["instances"]:
                result.setdefault(artifact["name"], {})[instance["checksum"]] = instance

        return result

    @property
    def referenced_artifacts(self):
        result = {}

        for artifact in self.get("artifacts", []):
            for instance in artifact["instances"]:
                if len(instance["references"]) == 0:
                    continue

                result.setdefault(artifact["name"], {})[instance["checksum"]] = instance

        return result

    @property
    def path(self):
        return Path(self["path"])


class Status(dict):
    @property
    def repos(self):
        result = {}

        for repo in self.get("artifact-repositories", []):
            result[repo["name"]] = RepoStatus(repo)

        return result


def get_status():
    out, err, exitcode = run("rauc status --output-format=json-pretty")
    assert exitcode == 0

    status = json.loads(out)

    assert status
    assert status.get("compatible") == "Test Config"

    return Status(status)


@contextmanager
def extracted_bundle(tmp_path, bundle_path):
    path = tmp_path / "extracted"
    assert not path.exists()

    try:
        out, err, exitcode = run(f"rauc --keyring openssl-ca/dev-ca.pem extract {bundle_path} {path}")
        assert exitcode == 0
        yield path
    finally:
        shutil.rmtree(path)


def test_bundle(tmp_path, bundle):
    """Create a bundle with artifacts and check the resulting information."""
    bundle.manifest["image.files/artifact-1"] = {
        "filename": "file-a.raw",
    }
    bundle.manifest["image.trees/artifact-1"] = {
        "filename": "tree-a.tar",
    }
    with open(bundle.content / "file-a.raw", "wb") as f:
        data_a = b"content-a"
        f.write(data_a)
    make_tarfile(bundle.content / "tree-a.tar", {"file": data_a})
    bundle.build()

    info = get_info(bundle.output)
    assert info["images"][0]["filename"] == "file-a.raw"
    assert info["images"][0]["slot-class"] == "files"
    assert info["images"][0]["artifact"] == "artifact-1"
    assert info["images"][1]["filename"] == "tree-a.tar"
    assert info["images"][1]["slot-class"] == "trees"
    assert info["images"][1]["artifact"] == "artifact-1"

    with extracted_bundle(tmp_path, bundle.output) as extracted:
        assert (extracted / "file-a.raw").is_file()
        assert (extracted / "tree-a.tar").is_file()


def test_bundle_convert_tree(tmp_path, bundle):
    """Create a bundle by extracting a tar file."""
    bundle.manifest["image.trees/artifact-1"] = {
        "filename": "tree-a.tar",
        "convert": "tar-extract",
    }
    data_a = b"content-a"
    make_tarfile(bundle.content / "tree-a.tar", {"file": data_a})
    bundle.build()

    info = get_info(bundle.output)
    pprint(info)
    assert info["images"][0]["filename"] == "tree-a.tar"
    assert info["images"][0]["slot-class"] == "trees"
    assert info["images"][0]["artifact"] == "artifact-1"
    assert info["images"][0]["convert"] == ["tar-extract"]
    assert info["images"][0]["converted"] == ["tree-a.tar.extracted"]

    with extracted_bundle(tmp_path, bundle.output) as extracted:
        run_tree(extracted)
        assert not (extracted / "tree-a.tar").exists()
        assert (extracted / "tree-a.tar.extracted").is_dir()
        assert (extracted / "tree-a.tar.extracted/file").is_file()
        with open(extracted / "tree-a.tar.extracted/file", "rb") as f:
            assert f.read() == data_a


def test_bundle_convert_tree_keep(tmp_path, bundle):
    """Create a bundle by extracting a tar file while keeping the original."""
    bundle.manifest["image.trees/artifact-1"] = {
        "filename": "tree-a.tar",
        "convert": "tar-extract;keep",
    }
    make_tarfile(bundle.content / "tree-a.tar", {"file": b"contents-a"})
    bundle.build()

    info = get_info(bundle.output)
    pprint(info)
    assert info["images"][0]["filename"] == "tree-a.tar"
    assert info["images"][0]["slot-class"] == "trees"
    assert info["images"][0]["artifact"] == "artifact-1"
    assert info["images"][0]["convert"] == ["tar-extract", "keep"]
    assert info["images"][0]["converted"] == ["tree-a.tar.extracted", "tree-a.tar"]

    with extracted_bundle(tmp_path, bundle.output) as extracted:
        assert (extracted / "tree-a.tar").exists()
        assert (extracted / "tree-a.tar.extracted").is_dir()
        assert (extracted / "tree-a.tar.extracted/file").is_file()
        with open(extracted / "tree-a.tar.extracted/file", "rb") as f:
            assert f.read() == b"contents-a"


def do_install_file(tmp_path, name, repo_name, artifact_name, artifact_data):
    bundle = Bundle(tmp_path, name)
    bundle.manifest[f"image.{repo_name}/{artifact_name}"] = {
        "filename": "file.raw",
    }
    with open(bundle.content / "file.raw", "wb") as f:
        f.write(artifact_data)
    bundle.build()

    out, err, exitcode = run(f"rauc install {bundle.output}")
    assert exitcode == 0

    bundle.output.unlink()


def do_install_tree(tmp_path, name, repo_name, artifact_name, artifact_contents):
    bundle = Bundle(tmp_path, name)
    bundle.manifest[f"image.{repo_name}/{artifact_name}"] = {
        "filename": "tree.tar",
    }
    make_tarfile(bundle.content / "tree.tar", artifact_contents)
    bundle.build()

    out, err, exitcode = run(f"rauc install {bundle.output}")
    assert exitcode == 0

    bundle.output.unlink()


def test_file_install(rauc_dbus_service_with_system, tmp_path):
    status = get_status()
    assert set(status.repos.keys()) == {"files", "trees"}
    assert set(status.repos["files"].artifacts) == set()
    assert set(status.repos["trees"].artifacts) == set()

    # install one file artifact and check result
    data_a = b"content-a"
    do_install_file(tmp_path, "a", "files", "artifact-1", data_a)

    status = get_status()
    assert "files" in status.repos
    repo = status.repos["files"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    with open(artifact_path, "rb") as f:
        assert f.read() == data_a
    assert artifact_path.samefile(Path("/run/rauc/artifacts/files/artifact-1"))

    # update one file artifact and check result
    data_b = b"content-b"
    do_install_file(tmp_path, "b", "files", "artifact-1", data_b)

    status = get_status()
    assert "files" in status.repos
    repo = status.repos["files"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    run_tree(repo.path)
    assert artifact_path.is_symlink()
    with open(artifact_path, "rb") as f:
        assert f.read() == data_b
    assert artifact_path.samefile(Path("/run/rauc/artifacts/files/artifact-1"))

    # install a different file artifact and check result
    data_c = b"content-c"
    do_install_file(tmp_path, "c", "files", "artifact-2", data_c)

    status = get_status()
    assert "files" in status.repos
    repo = status.repos["files"]
    assert "artifact-2" in repo.referenced_artifacts
    assert "artifact-1" not in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-2"
    assert artifact_path.is_symlink()
    with open(artifact_path, "rb") as f:
        assert f.read() == data_c
    assert artifact_path.samefile(Path("/run/rauc/artifacts/files/artifact-2"))
    artifact_path = repo.path / "artifact-1"
    assert not artifact_path.exists()
    assert not Path("/run/rauc/artifacts/trees/artifact-1").exists()


def test_tree_install(rauc_dbus_service_with_system, tmp_path):
    status = get_status()
    assert set(status.repos.keys()) == {"files", "trees"}
    assert set(status.repos["files"].artifacts) == set()
    assert set(status.repos["trees"].artifacts) == set()

    # install one tree artifact and check result
    data_a = b"content-a"
    do_install_tree(tmp_path, "a", "trees", "artifact-1", {"file-a": data_a})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-a", "rb") as f:
        assert f.read() == data_a
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-1"))

    # update one tree artifact and check result
    data_b = b"content-b"
    do_install_tree(tmp_path, "b", "trees", "artifact-1", {"file-b": data_b})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    run_tree(repo.path)
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-b", "rb") as f:
        assert f.read() == data_b
    # old file must be gone
    assert not (artifact_path / "file-a").exists()
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-1"))

    # install a different tree artifact and check result
    data_c = b"content-c"
    do_install_tree(tmp_path, "c", "trees", "artifact-2", {"file-a": data_c})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-2" in repo.referenced_artifacts
    assert "artifact-1" not in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-2"
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-a", "rb") as f:
        assert f.read() == data_c
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-2"))
    artifact_path = repo.path / "artifact-1"
    assert not artifact_path.exists()
    assert not Path("/run/rauc/artifacts/trees/artifact-1").exists()


def test_install_keep_other(rauc_dbus_service_with_system, tmp_path):
    """
    When we install to one repo, artifacts in other repos should not be
    removed.
    """
    status = get_status()
    assert set(status.repos.keys()) == {"files", "trees"}
    assert set(status.repos["files"].artifacts) == set()
    assert set(status.repos["trees"].artifacts) == set()

    # install one file artifact and check result
    data_a = b"content-a"
    do_install_file(tmp_path, "a", "files", "artifact-1", data_a)

    status = get_status()
    assert "files" in status.repos
    repo = status.repos["files"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    with open(artifact_path, "rb") as f:
        assert f.read() == data_a
    assert artifact_path.samefile(Path("/run/rauc/artifacts/files/artifact-1"))

    # install one tree artifact and check result
    data_b = b"content-b"
    do_install_tree(tmp_path, "b", "trees", "artifact-1", {"file-a": data_b})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-a", "rb") as f:
        assert f.read() == data_b
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-1"))

    # the file artifact should not be removed
    repo = status.repos["files"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    assert artifact_path.samefile(Path("/run/rauc/artifacts/files/artifact-1"))


def test_tree_in_use(rauc_dbus_service_with_system, tmp_path):
    status = get_status()
    assert set(status.repos.keys()) == {"files", "trees"}
    assert set(status.repos["files"].artifacts) == set()
    assert set(status.repos["trees"].artifacts) == set()

    # install one tree artifact and check result
    data_a = b"content-a"
    do_install_tree(tmp_path, "a", "trees", "artifact-1", {"file-a": data_a})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-1" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-1"
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-a", "rb") as f:
        assert f.read() == data_a
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-1"))

    # open a file from this artifact and remember the full path
    active_file_path = (artifact_path / "file-a").resolve()
    active_file = open(active_file_path, "rb")

    # install another tree artifact and check result
    data_b = b"content-b"
    do_install_tree(tmp_path, "b", "trees", "artifact-2", {"file-b": data_b})
    status = get_status()
    assert "trees" in status.repos
    repo = status.repos["trees"]
    assert "artifact-1" not in repo.referenced_artifacts
    assert "artifact-2" in repo.referenced_artifacts

    artifact_path = repo.path / "artifact-2"
    run_tree(repo.path)
    assert artifact_path.is_symlink()
    with open(artifact_path / "file-b", "rb") as f:
        assert f.read() == data_b
    # old file must be gone
    assert not (artifact_path / "file-a").exists()
    assert artifact_path.samefile(Path("/run/rauc/artifacts/trees/artifact-2"))

    active_file.close()
    with open(active_file_path, "rb") as f:
        assert f.read() == data_a
