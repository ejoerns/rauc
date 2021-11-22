#!/bin/bash

set -e

echo "Setting up for $1 test runs"

for run in $(seq 1 $1); do

echo "Starting test run $run..."

TMPDIR=$(mktemp -d)

# create target device
dd if=/dev/zero of=$TMPDIR/target-dev bs=1M count=10
mkfs.ext4 -F -I 256 $TMPDIR/target-dev > /dev/null
# create test image
dd if=/dev/random of=$TMPDIR/test-image bs=1M count=20
mkfs.ext4 -F -I 256 $TMPDIR/test-image > /dev/null

mount -t ext4 -v $TMPDIR/target-dev $TMPDIR/mount
umount -v $TMPDIR/target-dev

# verify not mounted
LOSETUP=$(losetup -j $TMPDIR/target-dev)
echo "$LOSETUP" | grep "/dev/loop" && echo "$LOSETUP" && exit 1

# copy content of image
cat $TMPDIR/test-image > $TMPDIR/target-dev

# remount for writing status file
mount -t ext4 -v $TMPDIR/target-dev $TMPDIR/mount
umount -v $TMPDIR/target-dev

echo "Test run $run done..."

rm -rf $TMPDIR

done
