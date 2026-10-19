#!/bin/sh
# Vendor mdev USB-stick handler, installed as both udisk_hotplug.sh (sd[a-z], add and
# remove) and udisk_insert.sh (sd[a-z][0-9], creation, also during "mdev -s").
# Mounts a partition, or a whole disk without partitions, by its detected type.
echo "/dev/$MDEV ${ACTION:-add}" >> /dev/console
case $ACTION in
	remove)
		umount -l /mnt/UPAN
		if [ "$(cat /tmp/update_status 2>/dev/null)" = "2" ]; then
			echo "Update Success, reboot now!!!" >> /dev/console
			reboot
		elif [ "$(cat /sys/class/android_usb_accessory/android0/functions 2>/dev/null)" = "mass_storage" ]; then
			echo "exit mass storage mode!!!" >> /dev/console
			rm -f /tmp/update_status /tmp/UDiskPassThroughMode
		fi
		;;
	*)
		[ -d /sys/block/*/$MDEV ] || [ ! -d /sys/block/*/$MDEV[0-9] ] || exit 0
		mkdir -p /mnt/UPAN
		# BusyBox blkid (1.36) has no -s/-o options: parse its record.
		INFO=$(blkid /dev/$MDEV 2>/dev/null)
		TYPE=$(printf '%s\n' "$INFO" | sed -n 's/.*TYPE="\([^"]*\)".*/\1/p')
		UUID=$(printf '%s\n' "$INFO" | sed -n 's/.*UUID="\([^"]*\)".*/\1/p')
		# The kernel has ntfs3, not the old ntfs driver.
		case "${TYPE:-vfat}" in
			vfat|msdos) TYPE=vfat OPTS="rw,umask=0000,utf8=1" ;;
			exfat) OPTS="rw,umask=0000" ;;
			ntfs|ntfs3) TYPE=ntfs3 OPTS="rw,umask=0000" ;;
			*) OPTS="rw" ;;
		esac
		if mount /dev/$MDEV /mnt/UPAN -t "$TYPE" -o "$OPTS"; then
			[ -n "$UUID" ] && printf '%s\n' "$UUID" > /tmp/udisk_uuid
			/script/update_box.sh /dev/$MDEV >> /dev/console &
		fi
		;;
esac
