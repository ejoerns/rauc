#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>

#include "bootchooser.h"
#include "context.h"
#include "raspberrypi.h"
#include "utils.h"

#define RASPBERRYPI_VCMAILBOX "vcmailbox"

static int rename_file(const gchar *oldfilename, const char *newfilename)
{
	int res;

	/* Try to exchange files... */
	res = renameat2(AT_FDCWD, oldfilename, AT_FDCWD, newfilename, RENAME_EXCHANGE);
	if (res == 0) {
		/* ... and remove old file. */
		if (g_remove(oldfilename) == -1) {
			int err = errno;
			g_warning("Failed to remove file %s: %s", oldfilename, g_strerror(err));
		}

		return 0;
	}

	/* ... or, try to replace file if filesystem does not support exchange. */
	if (res == -1 && errno == EINVAL)
		res = renameat2(AT_FDCWD, oldfilename, AT_FDCWD, newfilename, 0);

	return res;
}

static RaucSlot *raspberrypi_find_config_slot_by_autoboot_section(RaucConfig *config, const gchar *group_name, GError **error)
{
	g_autoptr(GKeyFile) key_file = NULL;
	GError *ierror = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *boot_partition = NULL;
	const gchar *filename;
	gsize length;
	RaucSlot *slot;

	g_return_val_if_fail(config, NULL);
	g_return_val_if_fail(group_name, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	filename = r_context()->config->raspberrypi_autoboottxt_path;
	if (!g_file_get_contents(filename, &data, &length, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to read %s: ", filename);
		return NULL;
	}

	key_file = g_key_file_new();
	if (!g_key_file_load_from_data(key_file, data, length, G_KEY_FILE_NONE, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to parse %s: ", filename);
		return NULL;
	}

	boot_partition = g_key_file_get_string(key_file, group_name, "boot_partition", &ierror);
	if (!boot_partition) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get 'boot_partition' in '[%s]' of %s: ", group_name, filename);
		return NULL;
	}
	if (boot_partition[0] == '\0') {
		g_set_error(
				error,
				R_BOOTCHOOSER_ERROR,
				R_BOOTCHOOSER_ERROR_PARSE_FAILED,
				"Empty 'boot_partition' in '[%s]' of %s", group_name, filename);
		return NULL;
	}

	slot = find_config_slot_by_bootname(config, boot_partition);
	if (!slot) {
		g_set_error(
				error,
				R_BOOTCHOOSER_ERROR,
				R_BOOTCHOOSER_ERROR_PARSE_FAILED,
				"No slot with bootname '%s' found for '[%s] boot_partition' in %s", boot_partition, group_name, filename);
		return NULL;
	}

	return slot;
}

static gboolean raspberrypi_bootloader_get(const gchar *property, guint *value, GError **error)
{
	g_auto(filedesc) fd = -1;
	g_autofree gchar *filename = NULL;
	guint32 val;

	g_return_val_if_fail(property, FALSE);
	g_return_val_if_fail(value, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	filename = g_build_filename("/sys/firmware/devicetree/base/chosen/bootloader", property, NULL);
	fd = g_open(filename, O_RDONLY);
	if (fd < 0) {
		g_set_error(
				error,
				R_BOOTCHOOSER_ERROR,
				R_BOOTCHOOSER_ERROR_PARSE_FAILED,
				"Failed to open file: %s", filename);
		return FALSE;
	}

	if (read(fd, &val, sizeof(val)) != sizeof(val)) {
		g_set_error(
				error,
				R_BOOTCHOOSER_ERROR,
				R_BOOTCHOOSER_ERROR_PARSE_FAILED,
				"Failed to read integer from file: %s", filename);
		return FALSE;
	}

	*value = g_htonl(val);

	return TRUE;
}

static gboolean raspberrypi_bootloader_get_partition(guint *partition, GError **error)
{
	return raspberrypi_bootloader_get("partition", partition, error);
}

static gboolean raspberrypi_get_reboot_flag(gboolean *enabled, GError **error)
{
	g_autoptr(GBytes) stdout_bytes = NULL;
	g_autoptr(GSubprocess) sub = NULL;
	g_autofree gchar *stdout_str = NULL;
	GError *ierror = NULL;

	g_return_val_if_fail(enabled, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/*
	 * The tag Get Reboot Flags is undocumented.
	 * https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
	 *
	 * However, it is defined by the raspberrypi-linux firmware driver:
	 * https://github.com/raspberrypi/linux/commit/e2726f05782135e15537575e95faea46c40a88a2
	 */
	sub = r_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror, RASPBERRYPI_VCMAILBOX,
			"0x00030064", "4", "0", "0", NULL);
	if (!sub) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to start " RASPBERRYPI_VCMAILBOX ": ");
		return FALSE;
	}

	if (!g_subprocess_communicate(sub, NULL, NULL, &stdout_bytes, NULL, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to run " RASPBERRYPI_VCMAILBOX ": ");
		return FALSE;
	}

	/*
	 * Parse output.
	 *
	 * If the reboot flag is unset:
	 *
	 * 	$ vcmailbox 0x00030064 4 0 0
	 * 	0x0000001c 0x80000000 0x00030064 0x00000004 0x80000004 0x00000000 0x00000000
	 *
	 * If the reboot flag is set:
	 *
	 * 	$ vcmailbox 0x00030064 4 0 0
	 * 	0x0000001c 0x80000000 0x00030064 0x00000004 0x80000004 0x00000001 0x00000000
	 */
	stdout_str = r_bytes_unref_to_string(&stdout_bytes);
	if (stdout_str) {
		g_auto(GStrv) words = g_strsplit(stdout_str, " ", -1);
		if (g_strv_length(words) > 5) {
			guint32 value = (guint32)g_ascii_strtoull(words[5], NULL, 0);
			*enabled = value == 0 ? FALSE : TRUE;
			return TRUE;
		}
	}

	g_set_error(
			error,
			R_BOOTCHOOSER_ERROR,
			R_BOOTCHOOSER_ERROR_PARSE_FAILED,
			"Failed to parse " RASPBERRYPI_VCMAILBOX ": %s", stdout_str);
	return FALSE;
}

static gboolean raspberrypi_set_reboot_flag(gboolean enable, GError **error)
{
	g_autoptr(GSubprocess) sub = NULL;
	GError *ierror = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/*
	 * The tag Set Reboot Flags is undocumented.
	 * https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
	 *
	 * However, it is used by the raspberrypi-linux firmware driver:
	 * https://github.com/raspberrypi/linux/commit/777a6a08bcf8f5f0a0086358dc66d
	 */
	sub = r_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &ierror, RASPBERRYPI_VCMAILBOX,
			"0x00038064", "4", "0", enable ? "1" : "0", NULL);
	if (!sub) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to start " RASPBERRYPI_VCMAILBOX ": ");
		return FALSE;
	}

	if (!g_subprocess_wait_check(sub, NULL, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to run " RASPBERRYPI_VCMAILBOX ": ");
		return FALSE;
	}

	return TRUE;
}

/* Write autoboot.txt with
 * 'persistent_bootname' as the [all] boot_partition and
 * 'tryboot_bootname' as the [tryboot] boot_partition. */
static gboolean raspberrypi_write_autoboot(gchar *persistent_bootname, gchar *tryboot_bootname, GError **error)
{
	g_auto(filedesc) fd = -1;
	g_autofree gchar *data = NULL;
	g_autofree gchar *filename_tmp = NULL;
	gchar *filename;
	gsize size;

	g_return_val_if_fail(persistent_bootname, FALSE);
	g_return_val_if_fail(tryboot_bootname, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	filename = r_context()->config->raspberrypi_autoboottxt_path;
	filename_tmp = g_strdup_printf("%s.tmp", filename);

	fd = g_open(filename_tmp, O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		int err = errno;
		g_set_error(
				error,
				G_FILE_ERROR,
				g_file_error_from_errno(err),
				"Failed to open file %s: %s", filename_tmp, g_strerror(err));
		return FALSE;
	}

	data = g_strdup_printf("[all]\ntryboot_a_b=1\nboot_partition=%s\n[tryboot]\nboot_partition=%s\n",
			persistent_bootname, tryboot_bootname);
	size = strlen(data);
	if (write(fd, data, size) != (gssize)size) {
		int err = errno;
		g_set_error(
				error,
				G_FILE_ERROR,
				g_file_error_from_errno(err),
				"Failed to write file %s: %s", filename_tmp, g_strerror(err));
		return FALSE;
	}

	if (fsync(fd) == -1) {
		int err = errno;
		g_set_error(
				error,
				G_FILE_ERROR,
				g_file_error_from_errno(err),
				"Failed to sync file %s: %s", filename_tmp, g_strerror(err));
		return FALSE;
	}

	if (rename_file(filename_tmp, filename) == -1) {
		int err = errno;
		g_set_error(
				error,
				G_FILE_ERROR,
				g_file_error_from_errno(err),
				"Failed to rename %s to %s: %s", filename_tmp, filename, g_strerror(err));
		return FALSE;
	}

	return TRUE;
}

static RaucSlot *raspberrypi_get_primary_and_reboot_flag(gboolean *reboot, GError **error)
{
	RaucSlot *primary;
	GError *ierror = NULL;

	g_return_val_if_fail(reboot, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!raspberrypi_get_reboot_flag(reboot, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get reboot flag: ");
		return NULL;
	}

	primary = raspberrypi_find_config_slot_by_autoboot_section(r_context()->config, *reboot ? "tryboot" : "all", &ierror);
	if (!primary) {
		g_propagate_error(error, ierror);
		return NULL;
	}

	return primary;
}

/* Get booted bootname */
gchar *r_raspberrypi_get_bootname(RaucConfig *config, GError **error)
{
	GError *ierror = NULL;
	guint partition;

	if (!raspberrypi_bootloader_get_partition(&partition, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get bootloader partition property: ");
		return NULL;
	}

	return g_strdup_printf("%u", partition);
}

/* Get slot marked as primary one, i.e. the slot with boot_partition set in the
 * section [all] in the file autoboot.txt if the reboot flag is unset, or the
 * slot with boot_partition set in the section [tryboot] in the file
 * autoboot.txt if the reboot flag is set. */
RaucSlot *r_raspberrypi_get_primary(GError **error)
{
	RaucSlot *primary;
	GError *ierror = NULL;
	gboolean reboot;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	primary = raspberrypi_get_primary_and_reboot_flag(&reboot, &ierror);
	if (!primary) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get primary slot and reboot flag: ");
		return NULL;
	}

	if (reboot)
		g_debug("Detected reboot flag");

	return primary;
}

/* Activate a slot for the next boot: if it is already the [all] default,
 * just cancel any pending one-shot switch; otherwise point [tryboot] at it
 * and arm the reboot flag (valid for the next boot only). */
gboolean r_raspberrypi_set_primary(RaucSlot *slot, GError **error)
{
	GError *ierror = NULL;

	RaucSlot *default_slot = raspberrypi_find_config_slot_by_autoboot_section(r_context()->config, "all", &ierror);
	if (!default_slot) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	if (slot == default_slot) {
		g_debug("set primary: Slot %s is already selected in [all] section.", slot->name);

		/* [all] already selects this slot, nothing to persist. But a
		 * previous set_primary() call for another slot may have armed
		 * the one-shot tryboot flag — cancel it so this slot is what
		 * actually boots next. */
		gboolean reboot;
		if (!raspberrypi_get_reboot_flag(&reboot, &ierror)) {
			g_propagate_prefixed_error(
					error,
					ierror,
					"Failed to get reboot flag: ");
			return FALSE;
		}

		if (!reboot)
			return TRUE;

		if (!raspberrypi_set_reboot_flag(FALSE, &ierror)) {
			g_propagate_prefixed_error(
					error,
					ierror,
					"Failed to clear reboot flag: ");
			return FALSE;
		}

		g_debug("set primary: Reboot flag cleared");
		return TRUE;
	}

	/* Write the autoboot.txt to ensure the slot is set in the [tryboot] section */
	if (!raspberrypi_write_autoboot(default_slot->bootname, slot->bootname, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to set %s as [tryboot] in autoboot.txt: ", slot->bootname);
		return FALSE;
	}

	/* Activate slot by setting the reboot flag.
	 * Note that the flag will be valid for the next boot, only. */
	if (!raspberrypi_set_reboot_flag(TRUE, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to set reboot flag: ");
		return FALSE;
	}

	g_debug("set primary: set [tryboot] entry and reboot flag for slot %s", slot->name);
	return TRUE;
}

/* A slot is good if it is selected in [all], or in [tryboot] while the reboot
 * flag is set. An unresolvable section is reported as an error. */
gboolean r_raspberrypi_get_state(RaucSlot *slot, gboolean *good, GError **error)
{
	GError *ierror = NULL;

	g_return_val_if_fail(slot, FALSE);
	g_return_val_if_fail(good, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	RaucSlot *all_slot = raspberrypi_find_config_slot_by_autoboot_section(r_context()->config, "all", &ierror);
	if (!all_slot) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	/* The [all] slot is bootable no matter what the reboot flag says. */
	if (slot == all_slot) {
		*good = TRUE;
		return TRUE;
	}

	/* Any other slot is only bootable while the reboot flag selects it. */
	gboolean reboot;
	if (!raspberrypi_get_reboot_flag(&reboot, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get reboot flag: ");
		return FALSE;
	}

	/* Without the reboot flag set, the [tryboot] section has no influence on
	 * what boots next, so do not require it to be resolvable. */
	if (reboot) {
		RaucSlot *tryboot_slot = raspberrypi_find_config_slot_by_autoboot_section(r_context()->config, "tryboot", &ierror);
		if (!tryboot_slot) {
			g_propagate_error(error, ierror);
			return FALSE;
		}
		if (slot == tryboot_slot) {
			*good = TRUE;
			return TRUE;
		}
	}

	*good = FALSE;
	return TRUE;
}

/* Persist a good slot as the new [all] default, demoting the previous default
 * to [tryboot]. Only commits the slot actually booted (per the devicetree
 * partition), so a later mark-good for a different slot cannot revert an
 * earlier commit made in the same boot session. Marking bad is a no-op. */
gboolean r_raspberrypi_set_state(RaucSlot *slot, gboolean good, GError **error)
{
	GError *ierror = NULL;
	RaucSlot *default_slot;
	guint partition;

	if (!good) {
		g_message("raspberrypi backend: setting boot state to 'bad' has no effect");
		return TRUE;
	}

	default_slot = raspberrypi_find_config_slot_by_autoboot_section(r_context()->config, "all", &ierror);
	if (!default_slot) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	/* The slot is already the default slot, nothing to persist. */
	if (slot == default_slot) {
		g_message("raspberrypi backend: setting boot state to 'good': already the default slot");
		return TRUE;
	}

	if (!raspberrypi_bootloader_get_partition(&partition, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to get bootloader 'partition' DTB property: ");
		return FALSE;
	}
	g_autofree gchar *partition_str = g_strdup_printf("%u", partition);
	if (g_strcmp0(slot->bootname, partition_str) != 0) {
		g_set_error(
				error,
				R_BOOTCHOOSER_ERROR,
				R_BOOTCHOOSER_ERROR_NOT_SUPPORTED,
				"Setting slot '%s' good is not supported: it is not the slot actually booted (partition %s)",
				slot->name, partition_str);
		return FALSE;
	}

	if (!raspberrypi_write_autoboot(slot->bootname, default_slot->bootname, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to persist slot '%s' in [all] section: ", slot->name);
		return FALSE;
	}
	g_debug("set good: Slot '%s' persisted in autoboot.txt [all] section", slot->name);

	if (!raspberrypi_set_reboot_flag(FALSE, &ierror)) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to clear reboot flag: ");
		return FALSE;
	}

	return TRUE;
}
