from helper import run
from conftest import no_service
from conftest import have_grub


@no_service
@have_grub
def test_status_mark_good_internally():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-good")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_bad_internally():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-bad")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_active_internally():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-active")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_good_booted():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-good booted")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_good_other():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-good other")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_good_any_bootslot():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-good rescue.0")

    assert exitcode == 0


@no_service
@have_grub
def test_status_mark_good_non_bootslot():
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc -c test.conf" " --override-boot-slot=system0" " status mark-good bootloader.0")

    assert exitcode == 1


@have_grub
def test_status_mark_good_dbus(rauc_service, rauc_dbus_service):
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc status mark-good")

    assert exitcode == 0
    assert "marked slot rootfs.0 as good" in out


@have_grub
def test_status_mark_bad_dbus(rauc_service, rauc_dbus_service):
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc status mark-bad")

    assert exitcode == 0
    assert "marked slot rootfs.0 as bad" in out


@have_grub
def test_status_mark_active_dbus(rauc_service, rauc_dbus_service):
    # Test callling 'rauc status'
    out, err, exitcode = run("rauc status mark-active")

    assert exitcode == 0
    assert "activated slot rootfs.0" in out
