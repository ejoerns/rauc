#!/bin/bash

set -e

echo "Setting up for $1 test runs"

TMPDIR=$(mktemp -d)
LOGFILE=$TMPDIR/test.log

# create target device
dd if=/dev/zero of=$TMPDIR/target-dev bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev > /dev/null
# create test image
dd if=/dev/zero of=$TMPDIR/test-image bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/test-image > /dev/null

# crate mount mounts
mkdir $TMPDIR/mount

# create minimal FS content
mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
mkdir -p /usr/bin /etc /lib /home/root
touch /usr/bin/file /etc/file /lib/file /home/root/file
umount $TMPDIR/target-dev

for run in $(seq 1 $1); do

echo "Run $run..." > $LOGFILE

# mimic slot status read
mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
touch $TMPDIR/mount/status.file || true
umount $TMPDIR/target-dev

# echo if loop associated
echo "TP@0: $(losetup -j $TMPDIR/target-dev)" > $LOGFILE

# copy content of image
dd if=$TMPDIR/test-image of=$TMPDIR/target-dev bs=1M

# echo if loop associated
echo "TP@1: $(losetup -j $TMPDIR/target-dev)" > $LOGFILE

# mount again for writing status file
mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
echo "Status file changed: $DATE" > $TMPDIR/mount/status.file
umount $TMPDIR/target-dev

echo "done." > $LOGFILE

done

# dump log
cat $LOGFILE

rm -rf $TMPDIR
