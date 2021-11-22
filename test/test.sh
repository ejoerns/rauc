#!/bin/bash

set -e

echo "Setting up for $1 test runs"

for run in $(seq 1 $1); do

echo "Starting test run $run..."

TMPDIR=$(mktemp -d)

# create target device
dd if=/dev/zero of=$TMPDIR/target-dev bs=1M count=10
/sbin/mkfs.ext4 -F -I 256 $TMPDIR/target-dev > /dev/null
# create test image
dd if=/dev/random of=$TMPDIR/test-image bs=1M count=20
/sbin/mkfs.ext4 -F -I 256 $TMPDIR/test-image > /dev/null

mkdir $TMPDIR/mount
mount -t ext4 -v $TMPDIR/target-dev $TMPDIR/mount
umount -d -v $TMPDIR/mount
losetup -d $TMPDIR/target-dev || true

# verify not mounted
LOSETUP=$(losetup -j $TMPDIR/target-dev)
#echo "LOSETUP"
echo "$LOSETUP" | grep "/dev/loop" && echo "HIT ISSUE!" && exit 1

echo "Test run $run done..."

rm -rf $TMPDIR

done
