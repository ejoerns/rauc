#!/bin/bash


set -e

echo "Setting up for $1 test runs"

TMPDIR=$(mktemp -d)
LOGFILE=$TMPDIR/test.log

echo "TMPDIR is $TMPDIR"
echo "LOGFILE is $LOGFILE"

# create target device
dd if=/dev/zero of=$TMPDIR/target-dev bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev > /dev/null
# create test image
dd if=/dev/zero of=$TMPDIR/test-image bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/test-image > /dev/null

# crate mount mounts
mkdir $TMPDIR/mount

# waste loop devs
dd if=/dev/zero of=$TMPDIR/target-dev-a bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-a > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-b bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-b > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-c bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-c > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-d bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-d > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-e bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-e > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-f bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-f > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-g bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-g > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-h bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-h > /dev/null
dd if=/dev/zero of=$TMPDIR/target-dev-i bs=1M count=50
mkfs.ext4 -F -I 256 $TMPDIR/target-dev-i > /dev/null

mkdir $TMPDIR/mount-a
mkdir $TMPDIR/mount-b
mkdir $TMPDIR/mount-c
mkdir $TMPDIR/mount-d
mkdir $TMPDIR/mount-e
mkdir $TMPDIR/mount-f
mkdir $TMPDIR/mount-g
mkdir $TMPDIR/mount-h
mkdir $TMPDIR/mount-i

mount -t ext4 $TMPDIR/target-dev-a $TMPDIR/mount-a
mount -t ext4 $TMPDIR/target-dev-b $TMPDIR/mount-b
mount -t ext4 $TMPDIR/target-dev-c $TMPDIR/mount-c
mount -t ext4 $TMPDIR/target-dev-d $TMPDIR/mount-d
mount -t ext4 $TMPDIR/target-dev-e $TMPDIR/mount-e
mount -t ext4 $TMPDIR/target-dev-f $TMPDIR/mount-f
mount -t ext4 $TMPDIR/target-dev-g $TMPDIR/mount-g
mount -t ext4 $TMPDIR/target-dev-h $TMPDIR/mount-h
mount -t ext4 $TMPDIR/target-dev-i $TMPDIR/mount-i

losetup -a

# create minimal FS content
#mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
#mkdir -p /usr/bin /etc /lib /home/root
#touch /usr/bin/file /etc/file /lib/file /home/root/file
#umount $TMPDIR/target-dev

#ps faux

for run in $(seq 1 $1); do

echo "Drop caches.." >> $LOGFILE
echo 1 > /proc/sys/vm/drop_caches

echo "Run $run..." >> $LOGFILE

# mimic slot status read
flock $TMPDIR/target-dev mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
touch $TMPDIR/mount/status.file || true
umount $TMPDIR/target-dev

# echo if loop associated
echo "TP@0: $(losetup -j $TMPDIR/target-dev)" >> $LOGFILE

#ps faux

# copy content of image
dd if=$TMPDIR/test-image of=$TMPDIR/target-dev bs=1M conv=fsync >> $LOGFILE 2>&1

# echo if loop associated
echo "TP@1: $(losetup -j $TMPDIR/target-dev)" >> $LOGFILE

echo "Drop caches.." >> $LOGFILE
#time echo 1 > /proc/sys/vm/drop_caches
sleep 0.5

#stat $TMPDIR/target-dev
#lslocks

# find non-overlapping device!
LOOPDEV=$(losetup --find -L --show $TMPDIR/target-dev)
echo "mounting $LOOPDEV"
mount -t ext4 $LOOPDEV $TMPDIR/mount

# mount again for writing status file
#flock $TMPDIR/target-dev mount -t ext4 $TMPDIR/target-dev $TMPDIR/mount
echo "Status file changed: $DATE" > $TMPDIR/mount/status.file
umount $TMPDIR/target-dev

echo "done." >> $LOGFILE

done

umount $TMPDIR/mount-a
umount $TMPDIR/mount-b
umount $TMPDIR/mount-c
umount $TMPDIR/mount-d
umount $TMPDIR/mount-e
umount $TMPDIR/mount-f
umount $TMPDIR/mount-g
umount $TMPDIR/mount-h
umount $TMPDIR/mount-i

# dump log
cat $LOGFILE

#m -rf $TMPDIR
