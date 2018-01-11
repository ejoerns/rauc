#include <config.h>

#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "context.h"
#include "mount.h"
#include "utils.h"

gboolean r_mount_full(const gchar *source, const gchar *mountpoint, const gchar* type, gsize size, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(10, g_free);
	
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
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run mount: ");
		goto out;
	}

	res = TRUE;
out:
	g_ptr_array_unref(args);
	return res;
}


gboolean r_mount_loop(const gchar *filename, const gchar *mountpoint, gsize size, GError **error) {
	return r_mount_full(filename, mountpoint, "squashfs", size, error);
}

gboolean r_umount(const gchar *filename, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(10, g_free);
	
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
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run umount: ");
		goto out;
	}

	res = TRUE;
out:
	g_ptr_array_unref(args);
	return res;
}

static void print_subprocess_call(GPtrArray *args) {
	g_message("%s", g_strjoinv(" ", args->pdata));
}

gboolean r_mount_casync(const gchar *source, const gchar *mountpoint, const gchar *store, const gchar **seeds, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(10, g_free);
	GDataInputStream *datainstream = NULL;
	gchar* outline;
	gint pid = 0;

	g_return_val_if_fail(source, FALSE);
	g_return_val_if_fail(mountpoint, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	
	g_ptr_array_add(args, g_strdup("notify-wait"));
	g_ptr_array_add(args, g_strdup("casync"));
	g_ptr_array_add(args, g_strdup("mount"));
	if (store) {
		g_ptr_array_add(args, g_strdup("--store"));
		g_ptr_array_add(args, g_strdup(store));
	}
	if (seeds) {
		for (gchar **seed = (gchar**) seeds; *seed != NULL; seed++) {
			g_ptr_array_add(args, g_strdup("--seed"));
			g_ptr_array_add(args, g_strdup(*seed));
		}
	}
	g_ptr_array_add(args, g_strdup(source));
	g_ptr_array_add(args, g_strdup(mountpoint));
	g_ptr_array_add(args, NULL);

	print_subprocess_call(args);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
				  G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to start casync: ");
		goto out;
	}

	datainstream = g_data_input_stream_new(g_subprocess_get_stdout_pipe(sproc));

	do {
		outline = g_data_input_stream_read_line(datainstream, NULL, NULL, NULL);
		if (!outline)
			continue;

		pid = g_ascii_strtoull(outline, NULL, 10);

	} while (outline);

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run casync: ");
		goto out;
	}

	if (pid == 0) {
		g_warning("Failed to get a valid PID");
	}


	res = TRUE;
out:
	g_ptr_array_unref(args);
	return res;
}



/* Creates a mount subdir in mount path prefix */
gchar* r_create_mount_point(const gchar *name, GError **error) {
	gchar* prefix;
	gchar* mountpoint = NULL;

	prefix = r_context()->config->mount_prefix;
	mountpoint = g_build_filename(prefix, name, NULL);

	if (!g_file_test (mountpoint, G_FILE_TEST_IS_DIR)) {
		gint ret;
		ret = g_mkdir_with_parents(mountpoint, 0700);

		if (ret != 0) {
			g_set_error(
					error,
					G_FILE_ERROR,
					G_FILE_ERROR_FAILED,
					"Failed creating mount path '%s'",
					mountpoint);
			g_free(mountpoint);
			mountpoint = NULL;
			goto out;
		}
	}

out:

	return mountpoint;
}

gboolean r_mount_slot(RaucSlot *slot, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;
	gchar *mount_point = NULL;

	g_assert_nonnull(slot);
	g_assert_null(slot->mount_point);

	if (!g_file_test(slot->device, G_FILE_TEST_EXISTS)) {
		g_set_error(
				error,
				G_FILE_ERROR,
				G_FILE_ERROR_NOENT,
				"Slot device '%s' not found",
				slot->device);
		goto out;
	}

	mount_point = r_create_mount_point(slot->name, &ierror);
	if (!mount_point) {
		res = FALSE;
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to create mount point: ");
		goto out;
	}

	res = r_mount_full(slot->device, mount_point, slot->type, 0, &ierror);
	if (!res) {
		res = FALSE;
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to mount slot: ");
		g_rmdir(mount_point);
		g_free(mount_point);
		goto out;
	}

	slot->mount_point = mount_point;

out:
	return res;
}

gboolean r_umount_slot(RaucSlot *slot, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_assert_nonnull(slot);
	g_assert_nonnull(slot->mount_point);

	res = r_umount(slot->mount_point, &ierror);
	if (!res) {
		res = FALSE;
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to unmount slot: ");
		goto out;
	}

	g_rmdir(slot->mount_point);
	g_clear_pointer(&slot->mount_point, g_free);

out:
	return res;
}
