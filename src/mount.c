#include <gio/gio.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "context.h"
#include "mount.h"
#include "utils.h"

gboolean r_mount_full(const gchar *source, const gchar *mountpoint, const gchar* type, gsize size, GError **error)
{
	g_autoptr(GSubprocess) sproc = NULL;
	GError *ierror = NULL;
	g_autoptr(GPtrArray) args = g_ptr_array_new_full(10, g_free);

	g_return_val_if_fail(source, FALSE);
	g_return_val_if_fail(mountpoint, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (getuid() != 0) {
		g_ptr_array_add(args, g_strdup("sudo"));
		g_ptr_array_add(args, g_strdup("--non-interactive"));
	}
	g_ptr_array_add(args, g_strdup("mount"));
	if (type != NULL) {
		g_ptr_array_add(args, g_strdup("-t"));
		g_ptr_array_add(args, g_strdup(type));
	}
	if (size != 0) {
		g_ptr_array_add(args, g_strdup("-o"));
		g_ptr_array_add(args, g_strdup_printf("ro,loop,sizelimit=%"G_GSIZE_FORMAT, size));
	}
	g_ptr_array_add(args, g_strdup(source));
	g_ptr_array_add(args, g_strdup(mountpoint));
	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			G_SUBPROCESS_FLAGS_NONE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to start mount: ");
		return FALSE;
	}

	if (!g_subprocess_wait_check(sproc, NULL, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run mount: ");
		return FALSE;
	}

	return TRUE;
}


gboolean r_mount_loop(const gchar *filename, const gchar *mountpoint, gsize size, GError **error)
{
	return r_mount_full(filename, mountpoint, "squashfs", size, error);
}

gboolean r_umount(const gchar *filename, GError **error)
{
	g_autoptr(GSubprocess) sproc = NULL;
	GError *ierror = NULL;
	g_autoptr(GPtrArray) args = g_ptr_array_new_full(10, g_free);

	g_return_val_if_fail(filename, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (getuid() != 0) {
		g_ptr_array_add(args, g_strdup("sudo"));
		g_ptr_array_add(args, g_strdup("--non-interactive"));
	}
	g_ptr_array_add(args, g_strdup("umount"));
	g_ptr_array_add(args, g_strdup(filename));
	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			G_SUBPROCESS_FLAGS_NONE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to start umount: ");
		return FALSE;
	}

	if (!g_subprocess_wait_check(sproc, NULL, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run umount: ");
		return FALSE;
	}

	return TRUE;
}


/* Creates a mount subdir in mount path prefix */
gchar* r_create_mount_point(const gchar *name, GError **error)
{
	g_autofree gchar *mountpoint = NULL;

	g_return_val_if_fail(name, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	mountpoint = g_build_filename(r_context()->config->mount_prefix, name, NULL);

	if (g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
		return g_steal_pointer(&mountpoint);

	if (g_mkdir_with_parents(mountpoint, 0700) != 0) {
		g_set_error(
				error,
				G_FILE_ERROR,
				G_FILE_ERROR_FAILED,
				"Failed creating mount path '%s'",
				mountpoint);
		return NULL;
	}

	return g_steal_pointer(&mountpoint);
}

gboolean r_mount_slot(RaucSlot *slot, GError **error)
{
	GError *ierror = NULL;
	g_autofree gchar *mount_point = NULL;

	g_return_val_if_fail(slot, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_assert_null(slot->mount_point);

	if (!g_file_test(slot->device, G_FILE_TEST_EXISTS)) {
		g_set_error(
				error,
				G_FILE_ERROR,
				G_FILE_ERROR_NOENT,
				"Slot device '%s' not found",
				slot->device);
		return FALSE;
	}

	mount_point = r_create_mount_point(slot->name, &ierror);
	if (!mount_point) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to create mount point: ");
		return FALSE;
	}

	if (!r_mount_full(slot->device, mount_point, slot->type, 0, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to mount slot: ");
		g_rmdir(mount_point);
		return FALSE;
	}

	slot->mount_point = g_steal_pointer(&mount_point);

	return TRUE;
}

gboolean r_umount_slot(RaucSlot *slot, GError **error)
{
	GError *ierror = NULL;

	g_return_val_if_fail(slot, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_assert_nonnull(slot->mount_point);

	if (!r_umount(slot->mount_point, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to unmount slot: ");
		return FALSE;
	}

	g_rmdir(slot->mount_point);
	g_clear_pointer(&slot->mount_point, g_free);

	return TRUE;
}
