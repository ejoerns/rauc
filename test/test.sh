#!/bin/bash

set -e

# create target device
dd if=/dev/zero of=target-dev bs=1M count=10
mkfs.ext4 -F -I 256 target-dev > /dev/null
# create test image
dd if=/dev/random of=test-image bs=1M count=10
mkfs.ext4 -F -I 256 test-image > /dev/null
# simulate installation
mount -v target-dev /mnt
echo "loop before:"
losetup -a | grep target-dev
stat /mnt/foo || true
umount -v target-dev

dd if=test-image of=target-dev
sync target-dev
echo "loop after:"
losetup -a | grep test-file || true
mount -v target-dev /mnt
umount -v target-dev
