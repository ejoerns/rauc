import shutil

from conftest import root


def test_method_info(rauc_service, rauc_dbus_service, tmp_path):
    shutil.copy("good-bundle.raucb", tmp_path / "good-bundle.raucb")

    out = rauc_dbus_service.Info(f"{tmp_path}/good-bundle.raucb")
    assert out[0] == "Test Config"
    assert out[1] == "2011.03-2"


def test_method_inspect_bundle(rauc_service, rauc_dbus_service, tmp_path):
    shutil.copy("good-bundle.raucb", tmp_path / "good-bundle.raucb")

    out = rauc_dbus_service.InspectBundle(f"{tmp_path}/good-bundle.raucb", [])
    assert "update" in out
    update = out["update"]
    assert update["compatible"] == "Test Config"
    assert update["version"] == "2011.03-2"


def test_method_get_primary(rauc_dbus_service, tmp_path):
    out = rauc_dbus_service.GetPrimary()
    assert out == "rootfs.1"


def test_method_get_slot_status(rauc_dbus_service, tmp_path):
    out = rauc_dbus_service.GetSlotStatus()
    assert len(out) == 4
    assert "class" in out[0][1]
    assert "class" in out[1][1]
    assert "class" in out[2][1]
    assert "class" in out[3][1]


@root
def test_method_install_bundle(rauc_service, rauc_dbus_service_with_system, tmp_path):
    last_percentage = 0
    proxy = rauc_dbus_service_with_system

    shutil.copy("good-verity-bundle.raucb", tmp_path / "good-verity-bundle.raucb")

    proxy.InstallBundle(f"{tmp_path}/good-verity-bundle.raucb", [])
    assert proxy.Operation == "installing"

    while True:
        percentage, message, depth = proxy.Progress
        assert percentage >= last_percentage

        if proxy.Operation != "installing":
            break

    assert proxy.Progress == (100, "Installing done.", 1)


def test_property_bootslot(rauc_dbus_service, tmp_path):
    assert rauc_dbus_service.BootSlot == "system0"


def test_property_compatible(rauc_dbus_service, tmp_path):
    assert rauc_dbus_service.Compatible == "Test Config"


def test_property_last_error(rauc_dbus_service, tmp_path):
    out = rauc_dbus_service.LastError
    print(out)


def test_property_operation(rauc_dbus_service, tmp_path):
    assert rauc_dbus_service.Operation == "idle"


def test_property_progress(rauc_dbus_service, tmp_path):
    assert rauc_dbus_service.Progress == (0, "", 0)


def test_property_variant(rauc_dbus_service, tmp_path):
    assert rauc_dbus_service.Variant == "Default Variant"
