#!/bin/bash

set -e

echo "Setting up for $1 test runs"

TMPDIR=$(mktemp -d)

# create target device
dd if=/dev/zero of=$TMPDIR/target-dev bs=1M count=2000
mkfs.ext4 -F -I 256 $TMPDIR/target-dev > /dev/null
# create test image
dd if=/dev/random of=$TMPDIR/test-image bs=1M count=1800
mkfs.ext4 -F -I 256 $TMPDIR/test-image > /dev/null

# crate mount mounts
mkdir $TMPDIR/mount

# create minimal FS content
mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
mkdir -p /usr/bin /etc /lib /home/root
touch /usr/bin/file /etc/file /lib/file /home/root/file
umount $TMPDIR/target-dev

for run in $(seq 1 $1); do

echo "Run $run..."

mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
touch $TMPDIR/mount/rauc.slots.bak || true
umount $TMPDIR/target-dev

# verify not mounted
#LOSETUP=$(losetup -j $TMPDIR/target-dev)
#echo "LOSETUP: $LOSETUP"
#echo "$LOSETUP" | grep "/dev/loop" && exit 1

# copy content of image
cat $TMPDIR/test-image > $TMPDIR/target-dev

# remount for writing status file
mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
echo "Status file changed" > $TMPDIR/mount/status.file
umount $TMPDIR/target-dev
# verify not mounted
#LOSETUP=$(losetup -j $TMPDIR/target-dev)
#echo "LOSETUP: $LOSETUP"
#echo "$LOSETUP" | grep "/dev/loop" && exit 1

echo "done."

done

rm -rf $TMPDIR
