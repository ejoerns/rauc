#!/bin/bash

set -e

echo "Starting test run..."

# create target device
dd if=/dev/zero of=target-dev bs=1M count=10
mkfs.ext4 -F -I 256 target-dev > /dev/null
# create test image
dd if=/dev/random of=test-image bs=1M count=20
mkfs.ext4 -F -I 256 test-image > /dev/null

# simulate installation

# mount and inspect for status file
mount -t ext4 -v target-dev /mnt
stat /mnt/rauc.slots || true
umount -v target-dev

# copy content of image
cat test-image > target-dev

# remount for writing status file
mount -t ext4 -v target-dev /mnt
umount -v target-dev

echo "Test run done..."
