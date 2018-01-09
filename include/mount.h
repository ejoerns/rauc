#pragma once

#include <glib.h>

#include "config_file.h"

/**
 * Wrapper for calling systems 'mount' command.
 *
 * If invoked as a user, mount command will be called using 'sudo'.
 *
 * @param source source path for mount
 * @param mountpoint destination path for mount
 * @param type type of image to mount (results in -t option)
 * @param size maximum size of image to mount (for loop mounts)
 * @param error return location for a GError, or NULL
 *
 * @return True if succeeded, False if failed
 */
gboolean r_mount_full(const gchar *source, const gchar *mountpoint, const gchar* type, gsize size, GError **error);

/**
 * Loopback mount a file.
 *
 * @param filename name of file to mount
 * @param mountpoint destination mount point
 * @param size limit accessable size of file, If 0, entire file is used
 * @param error return location for a GError, or NULL
 *
 * @return True if succeeded, False if failed
 */
gboolean r_mount_loop(const gchar *filename, const gchar *mountpoint, gsize size, GError **error);

/**
 * Unmount a slot or a file.
 *
 * @param dirdev directory or device to unmount
 * @param error return location for a GError, or NULL
 *
 * @return True if succeeded, False if failed
 */
gboolean r_umount(const gchar *dirdev, GError **error);

/**
 * Mont a casync file.
 *
 * @param source name of idx file to mount
 * @param mountpoint destination mount point
 * @param store path to casync store or NULL
 * @param seeds null-terminated array of paths to use as casync seed or NULL
 * @param error a GError object
 */
gboolean r_mount_casync(const gchar *source, const gchar *mountpoint, const gchar *store, const gchar **seeds, GError **error);

/**
 * Create a mount dir under mount prefix path.
 *
 * The directory will be created relative to the configured mount prefix path.
 *
 * @param name mount directory name to create
 * @param error return location for a GError, or NULL
 *
 * @return A newly allocated string containing the created mount path
 */
gchar* r_create_mount_point(const gchar *name, GError **error);

/**
 * Mount a slot.
 *
 * The mountpoint will be available as slot->mount_point.
 *
 * @param slot slot to mount
 * @param error return location for a GError, or NULL
 *
 * @return True if succeeded, False if failed
 */
gboolean r_mount_slot(RaucSlot *slot, GError **error);

/**
 * Unmount a slot.
 *
 * This only works for slots that were mounted by rauc.
 *
 * @param slot slot to unmount
 * @param error return location for a GError, or NULL
 *
 * @return True if succeeded, False if failed
 */
gboolean r_umount_slot(RaucSlot *slot, GError **error);
