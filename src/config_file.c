#include <glib.h>
#include <string.h>

#include "bootchooser.h"
#include "config_file.h"
#include "context.h"
#include "manifest.h"
#include "mount.h"
#include "utils.h"

G_DEFINE_QUARK(r-config-error-quark, r_config_error)
G_DEFINE_QUARK(r-slot-error-quark, r_slot_error)

#define RAUC_SLOT_PREFIX	"slot"

void default_config(RaucConfig **config)
{
	RaucConfig *c = g_new0(RaucConfig, 1);

	c->max_bundle_download_size = DEFAULT_MAX_BUNDLE_DOWNLOAD_SIZE;
	c->mount_prefix = g_strdup("/mnt/rauc/");
	/* When installing, we need a system.conf anyway, so this is used only
	 * for info/convert/extract/...
	 */
	c->bundle_formats_mask =
		1 << R_MANIFEST_FORMAT_PLAIN |
		        1 << R_MANIFEST_FORMAT_VERITY |
		        1 << R_MANIFEST_FORMAT_CRYPT;

	*config = c;
}

static gboolean fix_grandparent_links(GHashTable *slots, GError **error)
{
	/* Every child slot in a group must refer to the same parent.
	 * Some kind of grandparent relationship makes no sense, but if the
	 * user has accidentally constructed such a configuration we will fix
	 * it up for them here. */
	GHashTableIter iter;
	gpointer value;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		RaucSlot *slot = value;
		RaucSlot *realparent;
		unsigned int steps = 0;
		if (slot->parent == NULL)
			continue; /* not part of a group */
		realparent = slot->parent;
		while (realparent->parent) {
			realparent = realparent->parent;
			steps++;
			if (steps > 100) {
				g_set_error(
						error,
						R_CONFIG_ERROR,
						R_CONFIG_ERROR_PARENT_LOOP,
						"Slot '%s' has a parent loop!", slot->name);
				return FALSE;
			}
		}
		if (realparent != slot->parent) {
			g_message("Updating slot %s parent link to %s",
					slot->name,
					realparent->name);
			slot->parent = realparent;
		}
	}
	return TRUE;
}

gboolean parse_bundle_formats(guint *mask, const gchar *config, GError **error)
{
	gboolean res = TRUE;
	guint imask = 0;
	g_auto(GStrv) tokens = NULL;
	guint set = FALSE, modify = FALSE;

	g_return_val_if_fail(mask != NULL, FALSE);
	g_return_val_if_fail(config != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	imask = *mask;
	tokens = g_strsplit(config, " ", -1);

	for (size_t i = 0; tokens[i]; i++) {
		const gchar *token = tokens[i];
		gboolean plus = FALSE, minus = FALSE;
		RManifestBundleFormat format;

		if (!token[0]) /* empty string */
			continue;
		if (token[0] == '-') {
			minus = TRUE;
			token++;
		} else if (token[0] == '+') {
			plus = TRUE;
			token++;
		}

		if (g_strcmp0(token, "plain") == 0) {
			format = R_MANIFEST_FORMAT_PLAIN;
		} else if (g_strcmp0(token, "verity") == 0) {
			format = R_MANIFEST_FORMAT_VERITY;
		} else if (g_strcmp0(token, "crypt") == 0) {
			format = R_MANIFEST_FORMAT_CRYPT;
		} else {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_INVALID_FORMAT,
					"Invalid bundle format '%s'", token);
			res = FALSE;
			goto out;
		}

		if (plus || minus) {
			modify = TRUE;
		} else {
			if (!set)
				imask = 0;
			set = TRUE;
		}

		if (minus)
			imask &= ~(1 << format);
		else
			imask |= 1 << format;
	}

	if (set && modify) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_INVALID_FORMAT,
				"Invalid bundle format configuration '%s', cannot combine fixed value with modification (+/-)", config);
		res = FALSE;
		goto out;
	}

	if (!imask) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_INVALID_FORMAT,
				"Invalid bundle format configuration '%s', no remaining formats", config);
		res = FALSE;
		goto out;
	}

out:
	if (res)
		*mask = imask;
	return res;
}

static GHashTable *parse_slots(const char *filename, const char *data_directory, GKeyFile *key_file, GError **error)
{
	GError *ierror = NULL;
	g_auto(GStrv) groups = NULL;
	gsize group_count;
	g_autoptr(GHashTable) slots = NULL;
	g_autoptr(GList) slotlist = NULL;
	g_autoptr(GHashTable) bootnames = NULL;

	slots = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, r_slot_free);
	bootnames = g_hash_table_new(g_str_hash, g_str_equal);

	groups = g_key_file_get_groups(key_file, &group_count);
	for (gsize i = 0; i < group_count; i++) {
		g_auto(GStrv) groupsplit = g_strsplit(groups[i], ".", -1);

		/* We treat sections starting with "slot." as slots */
		if (g_str_equal(groupsplit[0], RAUC_SLOT_PREFIX)) {
			g_autoptr(RaucSlot) slot = g_new0(RaucSlot, 1);
			gchar* value;

			/* Assure slot strings consist of 3 parts, delimited by dots */
			if (g_strv_length(groupsplit) != 3) {
				g_set_error(
						error,
						R_CONFIG_ERROR,
						R_CONFIG_ERROR_INVALID_FORMAT,
						"Invalid slot name format: %s", groups[i]);
				return NULL;
			}

			value = g_strconcat(groupsplit[1], ".", groupsplit[2], NULL);
			slot->name = g_intern_string(value);
			g_free(value);

			/* If we have a data_directory, use a slot.<class>.<index>
			 * subdirectory for per-slot data. */
			if (data_directory)
				slot->data_directory = g_build_filename(data_directory, groups[i], NULL);

			slot->description = key_file_consume_string(key_file, groups[i], "description", NULL);

			slot->sclass = g_intern_string(groupsplit[1]);

			value = resolve_path_take(filename,
					key_file_consume_string(key_file, groups[i], "device", &ierror));
			if (!value) {
				g_propagate_error(error, ierror);
				return NULL;
			}
			slot->device = value;

			value = key_file_consume_string(key_file, groups[i], "type", NULL);
			if (!value)
				value = g_strdup("raw");
			slot->type = value;

			if (!r_slot_is_valid_type(slot->type)) {
				g_set_error(
						error,
						R_CONFIG_ERROR,
						R_CONFIG_ERROR_SLOT_TYPE,
						"Unsupported slot type '%s' for slot %s selected in system config", slot->type, slot->name);
				return NULL;
			}

			/* check if the device has an appropriate path */
			if (g_str_equal(slot->type, "jffs2") && !g_str_has_prefix(slot->device, "/dev/")) {
				g_set_error(
						error,
						R_CONFIG_ERROR,
						R_CONFIG_ERROR_INVALID_DEVICE,
						"%s: device must be located in /dev/ for jffs2", groups[i]);
				return NULL;
			}

			value = key_file_consume_string(key_file, groups[i], "bootname", NULL);
			slot->bootname = value;
			if (slot->bootname) {
				/* check if we have seen this bootname on another slot */
				if (g_hash_table_contains(bootnames, slot->bootname)) {
					g_set_error(
							error,
							R_CONFIG_ERROR,
							R_CONFIG_ERROR_DUPLICATE_BOOTNAME,
							"Bootname '%s' is set on more than one slot",
							slot->bootname);
					return NULL;
				}
				g_hash_table_add(bootnames, slot->bootname);
			}

			/* Collect name of parent here for easing remaining key checking.
			 * Will be resolved to slot->parent pointer after config parsing loop. */
			slot->parent_name = key_file_consume_string(key_file, groups[i], "parent", NULL);

			slot->allow_mounted = g_key_file_get_boolean(key_file, groups[i], "allow-mounted", &ierror);
			if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
				slot->allow_mounted = FALSE;
				g_clear_error(&ierror);
			} else if (ierror) {
				g_propagate_error(error, ierror);
				return NULL;
			}
			g_key_file_remove_key(key_file, groups[i], "allow-mounted", NULL);

			slot->readonly = g_key_file_get_boolean(key_file, groups[i], "readonly", &ierror);
			if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
				slot->readonly = FALSE;
				g_clear_error(&ierror);
			} else if (ierror) {
				g_propagate_error(error, ierror);
				return NULL;
			}
			g_key_file_remove_key(key_file, groups[i], "readonly", NULL);

			slot->install_same = g_key_file_get_boolean(key_file, groups[i], "install-same", &ierror);
			if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
				g_clear_error(&ierror);
				/* try also deprecated flag force-install-same */
				slot->install_same = g_key_file_get_boolean(key_file, groups[i], "force-install-same", &ierror);
				if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
					g_clear_error(&ierror);
					/* try also deprecated flag ignore-checksum */
					slot->install_same = g_key_file_get_boolean(key_file, groups[i], "ignore-checksum", &ierror);
					if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
						slot->install_same = TRUE;
						g_clear_error(&ierror);
					} else if (ierror) {
						g_propagate_error(error, ierror);
						return NULL;
					}
				} else if (ierror) {
					g_propagate_error(error, ierror);
					return NULL;
				}
			} else if (ierror) {
				g_propagate_error(error, ierror);
				return NULL;
			}
			g_key_file_remove_key(key_file, groups[i], "install-same", NULL);
			g_key_file_remove_key(key_file, groups[i], "force-install-same", NULL);
			g_key_file_remove_key(key_file, groups[i], "ignore-checksum", NULL);

			slot->extra_mount_opts = key_file_consume_string(key_file, groups[i], "extra-mount-opts", NULL);

			slot->resize = g_key_file_get_boolean(key_file, groups[i], "resize", &ierror);
			if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
				slot->resize = FALSE;
				g_clear_error(&ierror);
			} else if (ierror) {
				g_propagate_error(error, ierror);
				return NULL;
			}
			g_key_file_remove_key(key_file, groups[i], "resize", NULL);

			if (g_strcmp0(slot->type, "boot-mbr-switch") == 0 ||
			    g_strcmp0(slot->type, "boot-gpt-switch") == 0 ||
			    g_strcmp0(slot->type, "boot-raw-fallback") == 0) {
				slot->region_start = key_file_consume_binary_suffixed_string(key_file, groups[i],
						"region-start", &ierror);
				if (ierror) {
					g_propagate_prefixed_error(error, ierror, "mandatory for %s: ", slot->type);
					return NULL;
				}

				slot->region_size = key_file_consume_binary_suffixed_string(key_file, groups[i],
						"region-size", &ierror);
				if (ierror) {
					g_propagate_prefixed_error(error, ierror, "mandatory for %s: ", slot->type);
					return NULL;
				}
			}

			if (!check_remaining_keys(key_file, groups[i], &ierror)) {
				g_propagate_error(error, ierror);
				return NULL;
			}

			g_key_file_remove_group(key_file, groups[i], NULL);

			g_hash_table_insert(slots, (gchar*)slot->name, slot);
			slot = NULL;
		}
	}

	/* Add parent pointers */
	slotlist = g_hash_table_get_keys(slots);
	for (GList *l = slotlist; l != NULL; l = l->next) {
		RaucSlot *slot;
		RaucSlot *parent;
		RaucSlot *child;

		slot = g_hash_table_lookup(slots, l->data);
		if (!slot->parent_name) {
			continue;
		}

		parent = g_hash_table_lookup(slots, slot->parent_name);
		if (!parent) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_PARENT,
					"Parent slot '%s' not found!", slot->parent_name);
			return NULL;
		}

		child = g_hash_table_lookup(slots, l->data);
		child->parent = parent;

		if (child->bootname) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_CHILD_HAS_BOOTNAME,
					"Child slot '%s' has bootname set",
					child->name);
			return NULL;
		}
	}

	if (!fix_grandparent_links(slots, &ierror)) {
		g_propagate_error(error, ierror);
		return NULL;
	}

	return g_steal_pointer(&slots);
}

gboolean load_config(const gchar *filename, RaucConfig **config, GError **error)
{
	GError *ierror = NULL;
	g_autoptr(RaucConfig) c = g_new0(RaucConfig, 1);
	g_autoptr(GKeyFile) key_file = NULL;
	gboolean dtbvariant;
	g_autofree gchar *variant_data = NULL;
	g_autofree gchar *bundle_formats = NULL;

	g_return_val_if_fail(filename, FALSE);
	g_return_val_if_fail(config && *config == NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	key_file = g_key_file_new();

	if (!g_key_file_load_from_file(key_file, filename, G_KEY_FILE_NONE, &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	/* parse [system] section */
	c->system_compatible = key_file_consume_string(key_file, "system", "compatible", &ierror);
	if (!c->system_compatible) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	c->system_bootloader = key_file_consume_string(key_file, "system", "bootloader", NULL);
	if (!c->system_bootloader) {
		g_set_error_literal(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_BOOTLOADER,
				"No bootloader selected in system config");
		return FALSE;
	}

	if (!r_boot_is_supported_bootloader(c->system_bootloader)) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_BOOTLOADER,
				"Unsupported bootloader '%s' selected in system config", c->system_bootloader);
		return FALSE;
	}

	if (g_strcmp0(c->system_bootloader, "barebox") == 0) {
		c->system_bb_statename = key_file_consume_string(key_file, "system", "barebox-statename", NULL);
		c->system_bb_dtbpath = key_file_consume_string(key_file, "system", "barebox-dtbpath", NULL);
	} else if (g_strcmp0(c->system_bootloader, "grub") == 0) {
		c->grubenv_path = resolve_path_take(filename,
				key_file_consume_string(key_file, "system", "grubenv", NULL));
		if (!c->grubenv_path) {
			g_debug("No grubenv path provided, using /boot/grub/grubenv as default");
			c->grubenv_path = g_strdup("/boot/grub/grubenv");
		}
	} else if (g_strcmp0(c->system_bootloader, "efi") == 0) {
		c->efi_use_bootnext = g_key_file_get_boolean(key_file, "system", "efi-use-bootnext", &ierror);
		if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
			c->efi_use_bootnext = TRUE;
			g_clear_error(&ierror);
		} else if (ierror) {
			g_propagate_error(error, ierror);
			return FALSE;
		}
		g_key_file_remove_key(key_file, "system", "efi-use-bootnext", NULL);
	} else if (g_strcmp0(c->system_bootloader, "custom") == 0) {
		c->custom_bootloader_backend = resolve_path_take(filename,
				key_file_consume_string(key_file, "handlers", "bootloader-custom-backend", NULL));
		if (!c->custom_bootloader_backend) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_BOOTLOADER,
					"No custom bootloader backend defined");
			return FALSE;
		}
	}

	c->boot_default_attempts = key_file_consume_integer(key_file, "system", "boot-attempts", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		c->boot_default_attempts = 0; /* to indicate 'unset' */
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (c->boot_default_attempts < 0) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_BOOTLOADER,
				"Value for \"boot-attempts\" must not be negative");
		return FALSE;
	}

	c->boot_attempts_primary = key_file_consume_integer(key_file, "system", "boot-attempts-primary", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		c->boot_attempts_primary = 0; /* to indicate 'unset' */
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (c->boot_attempts_primary < 0) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_BOOTLOADER,
				"Value for \"boot-attempts-primary\" must not be negative");
		return FALSE;
	}
	if (c->boot_default_attempts > 0 || c->boot_attempts_primary > 0) {
		if ((g_strcmp0(c->system_bootloader, "uboot") != 0) && (g_strcmp0(c->system_bootloader, "barebox") != 0)) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_BOOTLOADER,
					"Configuring boot attempts is valid for uboot or barebox only (not for %s)", c->system_bootloader);
			return FALSE;
		}
	}

	c->max_bundle_download_size = g_key_file_get_uint64(key_file, "system", "max-bundle-download-size", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_debug("No value for key \"max-bundle-download-size\" in [system] defined "
				"- using default value of %d bytes.", DEFAULT_MAX_BUNDLE_DOWNLOAD_SIZE);
		c->max_bundle_download_size = DEFAULT_MAX_BUNDLE_DOWNLOAD_SIZE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (c->max_bundle_download_size == 0) {
		g_set_error(
				error,
				R_CONFIG_ERROR,
				R_CONFIG_ERROR_MAX_BUNDLE_DOWNLOAD_SIZE,
				"Invalid value (%" G_GUINT64_FORMAT ") for key \"max-bundle-download-size\" in system config", c->max_bundle_download_size);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "system", "max-bundle-download-size", NULL);

	c->mount_prefix = key_file_consume_string(key_file, "system", "mountprefix", NULL);
	if (!c->mount_prefix) {
		g_debug("No mount prefix provided, using /mnt/rauc/ as default");
		c->mount_prefix = g_strdup("/mnt/rauc/");
	}

	c->activate_installed = g_key_file_get_boolean(key_file, "system", "activate-installed", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		c->activate_installed = TRUE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "system", "activate-installed", NULL);

	c->system_variant_type = R_CONFIG_SYS_VARIANT_NONE;

	/* parse 'variant-dtb' key */
	dtbvariant = g_key_file_get_boolean(key_file, "system", "variant-dtb", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		dtbvariant = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "system", "variant-dtb", NULL);
	if (dtbvariant)
		c->system_variant_type = R_CONFIG_SYS_VARIANT_DTB;

	/* parse 'variant-file' key */
	variant_data = key_file_consume_string(key_file, "system", "variant-file", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_clear_pointer(&variant_data, g_free);
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (variant_data) {
		if (c->system_variant_type != R_CONFIG_SYS_VARIANT_NONE) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_INVALID_FORMAT,
					"Only one of the keys 'variant-file', variant-dtb','variant-name' is allowed");
			return FALSE;
		}

		c->system_variant_type = R_CONFIG_SYS_VARIANT_FILE;
		c->system_variant = g_steal_pointer(&variant_data);
	}

	/* parse 'variant-name' key */
	variant_data = key_file_consume_string(key_file, "system", "variant-name", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		variant_data = NULL;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (variant_data) {
		if (c->system_variant_type != R_CONFIG_SYS_VARIANT_NONE) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_INVALID_FORMAT,
					"Only one of the keys 'variant-file', variant-dtb','variant-name' is allowed");
			return FALSE;
		}

		c->system_variant_type = R_CONFIG_SYS_VARIANT_NAME;
		c->system_variant = g_steal_pointer(&variant_data);
	}

	/* parse data/status location
	 *
	 * We have multiple levels of backwards compatibility:
	 * - per-slot status and no shared data directory
	 *   (by default or explicitly with ``statusfile=per-slot``)
	 * - central status file and no shared data directory
	 *   (``statusfile=/data/central.raucs``)
	 * - central status file and shared data directory
	 *   (``statusfile=/data/central.raucs`` and ``data-directory=/data/rauc``)
	 * - central status file in shared data directory
	 *   (``data-directory=/data/rauc``, implies ``statusfile=/data/rauc/central.raucs``)
	 */
	c->data_directory = resolve_path_take(filename,
			key_file_consume_string(key_file, "system", "data-directory", &ierror));
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	c->statusfile_path = key_file_consume_string(key_file, "system", "statusfile", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		if (c->data_directory) {
			c->statusfile_path = g_build_filename(c->data_directory, "central.raucs", NULL);
		} else {
			g_message("Config option 'statusfile=<path>/per-slot' unset, falling back to per-slot status");
			c->statusfile_path = g_strdup("per-slot");
		}
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	if (g_strcmp0(c->statusfile_path, "per-slot") == 0) {
		if (c->data_directory) {
			g_set_error(
					error,
					R_CONFIG_ERROR,
					R_CONFIG_ERROR_DATA_DIRECTORY,
					"Using data-directory= with statusfile=per-slot is not supported.");
			return FALSE;
		}
		g_message("Using per-slot statusfile");
	} else {
		gchar *resolved = resolve_path(filename, c->statusfile_path);
		g_free(c->statusfile_path);
		c->statusfile_path = resolved;
		g_message("Using central status file %s", c->statusfile_path);
	}

	/* parse bundle formats */
	c->bundle_formats_mask =
		1 << R_MANIFEST_FORMAT_PLAIN |
		        1 << R_MANIFEST_FORMAT_VERITY |
		        1 << R_MANIFEST_FORMAT_CRYPT;
	bundle_formats = key_file_consume_string(key_file, "system", "bundle-formats", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	} else {
		if (!parse_bundle_formats(&c->bundle_formats_mask, bundle_formats, &ierror)) {
			g_propagate_error(error, ierror);
			return FALSE;
		}
	}

	c->perform_pre_check = g_key_file_get_boolean(key_file, "system", "perform-pre-check", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
		c->perform_pre_check = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "system", "perform-pre-check", NULL);

	if (!check_remaining_keys(key_file, "system", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "system", NULL);

	/* parse [keyring] section */
	c->keyring_path = resolve_path_take(filename,
			key_file_consume_string(key_file, "keyring", "path", NULL));
	c->keyring_directory = resolve_path_take(filename,
			key_file_consume_string(key_file, "keyring", "directory", NULL));

	c->keyring_check_crl = g_key_file_get_boolean(key_file, "keyring", "check-crl", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
	    g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		c->keyring_check_crl = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "keyring", "check-crl", NULL);

	c->keyring_allow_partial_chain = g_key_file_get_boolean(key_file, "keyring", "allow-partial-chain", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
	    g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		c->keyring_allow_partial_chain = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "keyring", "allow-partial-chain", NULL);

	c->use_bundle_signing_time = g_key_file_get_boolean(key_file, "keyring", "use-bundle-signing-time", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
	    g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		c->use_bundle_signing_time = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "keyring", "use-bundle-signing-time", NULL);

	c->keyring_check_purpose = key_file_consume_string(key_file, "keyring", "check-purpose", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
	    g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		c->keyring_check_purpose = NULL;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	if (!check_remaining_keys(key_file, "keyring", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "keyring", NULL);

	/* parse [casync] section */
	c->store_path = key_file_consume_string(key_file, "casync", "storepath", NULL);
	c->tmp_path = key_file_consume_string(key_file, "casync", "tmppath", NULL);
	c->casync_install_args = key_file_consume_string(key_file, "casync", "install-args", NULL);
	c->use_desync = g_key_file_get_boolean(key_file, "casync", "use-desync", &ierror);
	if (g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
	    g_error_matches(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
		c->use_desync = FALSE;
		g_clear_error(&ierror);
	} else if (ierror) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_key(key_file, "casync", "use-desync", NULL);
	if (!check_remaining_keys(key_file, "casync", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "casync", NULL);

	/* parse [streaming] section */
	c->streaming_sandbox_user = key_file_consume_string(key_file, "streaming", "sandbox-user", NULL);
	c->streaming_tls_cert = key_file_consume_string(key_file, "streaming", "tls-cert", NULL);
	c->streaming_tls_key = key_file_consume_string(key_file, "streaming", "tls-key", NULL);
	c->streaming_tls_ca = key_file_consume_string(key_file, "streaming", "tls-ca", NULL);
	if (!check_remaining_keys(key_file, "streaming", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "streaming", NULL);

	/* parse [encryption] section */
	c->encryption_key = resolve_path_take(filename,
			key_file_consume_string(key_file, "encryption", "key", NULL));
	c->encryption_cert = resolve_path_take(filename,
			key_file_consume_string(key_file, "encryption", "cert", NULL));
	if (!check_remaining_keys(key_file, "encryption", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "encryption", NULL);

	/* parse [autoinstall] section */
	c->autoinstall_path = resolve_path_take(filename,
			key_file_consume_string(key_file, "autoinstall", "path", NULL));
	if (!check_remaining_keys(key_file, "autoinstall", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "autoinstall", NULL);

	/* parse [handlers] section */
	c->systeminfo_handler = resolve_path_take(filename,
			key_file_consume_string(key_file, "handlers", "system-info", NULL));

	c->preinstall_handler = resolve_path_take(filename,
			key_file_consume_string(key_file, "handlers", "pre-install", NULL));

	c->postinstall_handler = resolve_path_take(filename,
			key_file_consume_string(key_file, "handlers", "post-install", NULL));
	if (!check_remaining_keys(key_file, "handlers", &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	g_key_file_remove_group(key_file, "handlers", NULL);

	/* parse [slot.*.#] sections */
	c->slots = parse_slots(filename, c->data_directory, key_file, &ierror);
	if (!c->slots) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	if (!check_remaining_groups(key_file, &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	/* on success, return config struct */
	*config = g_steal_pointer(&c);

	return TRUE;
}

RaucSlot *find_config_slot_by_device(RaucConfig *config, const gchar *device)
{
	g_return_val_if_fail(config, NULL);

	return r_slot_find_by_device(config->slots, device);
}

RaucSlot *find_config_slot_by_name(RaucConfig *config, const gchar *name)
{
	g_return_val_if_fail(config, NULL);
	g_return_val_if_fail(config->slots, NULL);
	g_return_val_if_fail(name, NULL);

	return g_hash_table_lookup(config->slots, name);
}

void free_config(RaucConfig *config)
{
	if (!config)
		return;

	g_free(config->system_compatible);
	g_free(config->system_variant);
	g_free(config->system_bootloader);
	g_free(config->system_bb_statename);
	g_free(config->system_bb_dtbpath);
	g_free(config->mount_prefix);
	g_free(config->store_path);
	g_free(config->tmp_path);
	g_free(config->casync_install_args);
	g_free(config->grubenv_path);
	g_free(config->statusfile_path);
	g_free(config->keyring_path);
	g_free(config->keyring_directory);
	g_free(config->keyring_check_purpose);
	g_free(config->autoinstall_path);
	g_free(config->systeminfo_handler);
	g_free(config->preinstall_handler);
	g_free(config->postinstall_handler);
	g_free(config->streaming_sandbox_user);
	g_free(config->streaming_tls_cert);
	g_free(config->streaming_tls_key);
	g_free(config->streaming_tls_ca);
	g_free(config->encryption_key);
	g_free(config->encryption_cert);
	g_clear_pointer(&config->slots, g_hash_table_destroy);
	g_free(config);
}
