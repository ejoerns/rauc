#include "update_handler.h"
#include "mount.h"

#include <gio/gunixoutputstream.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <mtd/ubi-user.h>

#define R_UPDATE_ERROR r_update_error_quark ()

static GQuark r_update_error_quark (void)
{
	return g_quark_from_static_string ("r_update_error_quark");
}

GHashTable *fs_type_handlers = NULL;

gboolean register_fstype_handler(const gchar* fstype, const gchar* imgtype, img_to_fs_handler handler)
{

	gboolean res = FALSE;
	GHashTable *fs_img_type_handlers = NULL;

	g_assert_nonnull(handler);

	if (g_hash_table_contains(fs_type_handlers, fstype)) {
		fs_img_type_handlers = (GHashTable*) g_hash_table_lookup(fs_type_handlers, fstype);
	} else {
		fs_img_type_handlers = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL); // TODO: free?
		res = g_hash_table_insert(fs_type_handlers, g_strdup(fstype), (gboolean*) fs_img_type_handlers);
		if (!res) {
			g_warning("failed to register fstype: %s", fstype);
			goto out;
		}
		g_message("registered new fstype: %s", fstype);
	}

	if (g_hash_table_contains(fs_img_type_handlers, imgtype)) {
		g_message("skipping duplicate image entry for fstype %s: %s", fstype, imgtype);
		goto out;
	}

	res = g_hash_table_insert(fs_img_type_handlers, g_strdup(imgtype), handler);
	if (!res) {
		g_warning("failed to register imagetype handler for fstype=%s: %s)", fstype, imgtype);
		goto out;
	}
	g_message("registered new imgtype handler for fstype=%s: %s)", fstype, imgtype);

	res = TRUE;

out:
	return res;
}


img_to_fs_handler check_for_update_handler(RaucImage *mfimage, RaucSlot  *dest_slot, GError **error) {

	GHashTable *fs_img_type_handlers = NULL;
	GList *list;
	gchar *imgtype = NULL;
	img_to_fs_handler handler = NULL;

	fs_img_type_handlers = g_hash_table_lookup(fs_type_handlers, dest_slot->type);
	if (fs_img_type_handlers == NULL) {
		g_set_error(error, R_UPDATE_ERROR, 0, "No update handler for target slot type (%s)", dest_slot->type);
		goto out;
	}
	g_message("Checking image type for slot type: %s", dest_slot->type);

	list = g_hash_table_get_keys(fs_img_type_handlers);
	for (GList *l = list; l != NULL; l = l->next) {
		GRegex *regex;
		GMatchInfo *match_info;
		gchar *regexstr;

		g_message("Checking for suffix: %s", (gchar*)l->data);

		/* Let image name match end of bundle path name */
		regexstr = g_strdup_printf(".%s$", (gchar*)l->data);
		regex = g_regex_new (regexstr, 0, 0, NULL);
		g_regex_match (regex, mfimage->filename, 0, &match_info);
		if (g_match_info_matches (match_info))
		{
			imgtype = (gchar*) l->data;
			g_message("Image detected as type: %s\n", imgtype);
			break;
		}
		//g_match_info_free (match_info);
		//g_regex_unref (regex);
	}

	if (imgtype == NULL)  {
		g_set_error(error, R_UPDATE_ERROR, 1, "Unable to detect supported image type for %s", mfimage->filename);
		goto out;
	}

	handler = g_hash_table_lookup(fs_img_type_handlers, imgtype);

out:
	return handler;

}

GOutputStream* open_slot_device(RaucSlot *slot, int *fd, GError **error);

GOutputStream* open_slot_device(RaucSlot *slot, int *fd, GError **error) {

	GOutputStream *outstream = NULL;
	GFile *destslotfile = NULL;
	GError *ierror = NULL;
	int fd_out;

	destslotfile = g_file_new_for_path(slot->device);

	fd_out = open(g_file_get_path(destslotfile), O_WRONLY);

	if (fd_out == -1) {
		g_set_error(error, R_UPDATE_ERROR, 0,
				"opening output device failed: %s", strerror(errno));
		goto out;
	}

	outstream = g_unix_output_stream_new(fd_out, TRUE);
	if (outstream == NULL) {
		g_propagate_prefixed_error(error, ierror,
				"failed to open file for writing: ");
		goto out;
	}

	if (fd != NULL)
		*fd = fd_out;

out:

	return outstream;
}

static gboolean ubifs_ioctl(RaucImage *image, int fd, GError **error) {
	int ret;

	/* set up ubi volume for image copy */
	ret = ioctl(fd, UBI_IOCVOLUP, image->checksum.size);
	if (ret == -1) {
		g_set_error(error, R_UPDATE_ERROR, 0,
				"ubi volume update failed: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


gboolean dd_image(RaucImage *image, GOutputStream *outstream, GError **error);

gboolean dd_image(RaucImage *image, GOutputStream *outstream, GError **error) {

	GError *ierror = NULL;
	gssize size;
	GInputStream *instream = NULL;

	GFile *srcimagefile = NULL;

	srcimagefile = g_file_new_for_path(image->filename);

	instream = (GInputStream*)g_file_read(srcimagefile, NULL, &ierror);
	if (instream == NULL) {
		g_propagate_prefixed_error(error, ierror,
				"failed to open file for reading: ");
		goto out;
	}

	size = g_output_stream_splice(
			outstream,
			instream,
			G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
			NULL,
			&ierror);
	if (size == -1) {
		g_propagate_prefixed_error(error, ierror,
				"failed splicing data: ");
		goto out;
	} else if (size != (gssize) image->checksum.size) {
		g_set_error_literal(error, R_UPDATE_ERROR, 0,
				"image size and written size differ!");
		goto out;
	}
out:
	return TRUE;
}


gboolean ubifs_format_slot(RaucSlot *dest_slot, GError **error);

gboolean ubifs_format_slot(RaucSlot *dest_slot, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(3, g_free);
	
	g_ptr_array_add(args, g_strdup("mkfs.ubifs"));
	g_ptr_array_add(args, g_strdup("-y"));
	g_ptr_array_add(args, g_strdup(dest_slot->device));
	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
				  G_SUBPROCESS_FLAGS_NONE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to start mkfs.ubifs: ");
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run mkfs.ubifs: ");
		goto out;
	}

	res = TRUE;
out:
	g_ptr_array_unref(args);
	return res;
}


gboolean untar_image(RaucImage *image, gchar *dest, GError **error);

gboolean untar_image(RaucImage *image, gchar *dest, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(3, g_free);

	g_assert_nonnull(image);
	g_assert_nonnull(dest);
	
	g_ptr_array_add(args, g_strdup("tar"));
	g_ptr_array_add(args, g_strdup("xf"));
	g_ptr_array_add(args, g_strdup(image->filename));
	g_ptr_array_add(args, g_strdup("-C"));
	g_ptr_array_add(args, g_strdup(dest));
	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
				  G_SUBPROCESS_FLAGS_NONE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to start tar extract: ");
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to run tar extract: ");
		goto out;
	}

	res = TRUE;
out:
	g_ptr_array_unref(args);
	return res;
}

static gboolean ubifs_to_ubifs_handler(RaucImage *image, RaucSlot *dest_slot, GError **error) {

	GOutputStream *outstream = NULL;
	GError *ierror = NULL;
	int out_fd;
	gboolean res = FALSE;

	/* open */
	g_message("opening slot device %s", dest_slot->device);
	outstream = open_slot_device(dest_slot, &out_fd, &ierror);
	if (outstream == NULL) {
		res = FALSE;
		g_propagate_error(error, ierror);
		goto out;
	}

	/* ubifs ioctl */
	res = ubifs_ioctl(image, out_fd, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	/* copy */

	res = dd_image(image, outstream, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:	
	return res;
}

static gboolean tar_to_ubifs_handler(RaucImage *image, RaucSlot *dest_slot, GError **error) {

	GError *ierror = NULL;
	gboolean res = FALSE;
	gchar* mountpoint;

	/* format ubi volume */
	g_message("Formatting ubifs slot %s", dest_slot->device);
	res = ubifs_format_slot(dest_slot, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	mountpoint = r_create_mount_point("image", &ierror);
	if (!mountpoint) {
		res = FALSE;
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed creating mount point: ");
		goto out;
	}

	/* mount ubi volume */
	g_message("Mounting ubifs slot %s to %s", dest_slot->device, mountpoint);
	res = r_mount_slot(dest_slot, mountpoint, &ierror);
	if (!res) {
		g_message("Mounting failed: %s", ierror->message);
		g_clear_error(&ierror);
		goto unmount_out;
	}

	/* extract tar into mounted ubi volume */
	g_message("Extracting %s to %s", image->filename, mountpoint);
	res = untar_image(image, mountpoint, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto unmount_out;
	}

unmount_out:

	/* finally umount ubi volume */
	g_message("Unmounting ubifs slot %s", dest_slot->device);
	if (!r_umount(mountpoint, &ierror)) {
		res = FALSE;
		g_warning("Unmounting failed: %s", ierror->message);
		g_clear_error(&ierror);
	}
	
out:	

	return res;
}

static gboolean ext4_to_raw_handler(RaucImage *image, RaucSlot *dest_slot, GError **error) {
	// ...
	// ...
	return TRUE;
}

static gboolean vfat_to_raw_handler(RaucImage *image, RaucSlot *dest_slot, GError **error) {
	// ...
	// ...
	return TRUE;
}

static gboolean ext4_to_ext4_handler(RaucImage *image, RaucSlot *dest_slot, GError **error) {
	// ...
	// ...
	return TRUE;
}


void setup_fstype_handlers(void) {

	if (fs_type_handlers != NULL)
		return;

	fs_type_handlers = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL); // TODO: free?

	register_fstype_handler("ubifs", "tar.*", tar_to_ubifs_handler);
	register_fstype_handler("ubifs", "ubifs", ubifs_to_ubifs_handler);
	register_fstype_handler("ubifs", "foo", ubifs_to_ubifs_handler);
	register_fstype_handler("raw", "ext4", ext4_to_raw_handler);
	register_fstype_handler("raw", "vfat", vfat_to_raw_handler);
	register_fstype_handler("ext4", "ext4", ext4_to_ext4_handler);

}

