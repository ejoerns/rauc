#include <glib.h>

#include "bootchooser.h"
#include "context.h"
#include "global_state.h"

gboolean r_global_state_load(const gchar *filename, RGlobalState *state, GError **error)
{
	g_autoptr(GKeyFile) key_file = NULL;
	GError *ierror = NULL;

	key_file = g_key_file_new();

	if (!g_key_file_load_from_file(key_file, filename, G_KEY_FILE_NONE, &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	state->boot_id = g_key_file_get_string(key_file, "system", "boot-id", NULL);

	return TRUE;
}

gboolean r_global_state_save(const gchar *filename, RGlobalState *state, GError **error)
{
	g_autoptr(GKeyFile) key_file = NULL;
	GError *ierror = NULL;

	key_file = g_key_file_new();

	g_key_file_set_string(key_file, "system", "boot-id", state->boot_id);

	if (!g_key_file_save_to_file(key_file, filename, &ierror)) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	return TRUE;
}

void r_global_state_free(RGlobalState *state) {
	g_free(state->boot_id);
	g_free(state);
}

/*
 * Based on the 'bootslot' information (derived from /proc/cmdline during
 * context setup), this determines 'booted', 'active' and 'inactive' states for
 * each slot and stores this in the 'state' member of each slot.
 *
 * First, the booted slot is determined by comparing the 'bootslot' against the
 * slot's 'bootname', 'name', or device path. Then, the other states are
 * determined based on the slot hierarchies.
 *
 * If 'bootslot' is '/dev/nfs' or '_external_', all slots are considered
 * 'inactive'.
 *
 * @param error Return location for a GError, or NULL
 *
 * @return TRUE if succeeded, FALSE if failed
 */
gboolean determine_slot_states(GError **error)
{
	g_autoptr(GList) slotlist = NULL;
	RaucSlot *booted = NULL;

	g_assert_nonnull(r_context()->config);

	if (r_context()->config->slots == NULL) {
		g_set_error_literal(
				error,
				R_SLOT_ERROR,
				R_SLOT_ERROR_NO_CONFIG,
				"No slot configuration found");
		return FALSE;
	}

	if (r_context()->bootslot == NULL) {
		g_set_error_literal(
				error,
				R_SLOT_ERROR,
				R_SLOT_ERROR_NO_BOOTSLOT,
				"Could not find any root device or rauc slot information in /proc/cmdline");
		return FALSE;
	}

	slotlist = g_hash_table_get_keys(r_context()->config->slots);

	for (GList *l = slotlist; l != NULL; l = l->next) {
		g_autofree gchar *realdev = NULL;
		RaucSlot *s = g_hash_table_lookup(r_context()->config->slots, l->data);
		g_assert_nonnull(s);

		if (g_strcmp0(s->bootname, r_context()->bootslot) == 0) {
			booted = s;
			break;
		}

		if (g_strcmp0(s->name, r_context()->bootslot) == 0) {
			booted = s;
			break;
		}

		realdev = r_realpath(s->device);
		if (realdev == NULL) {
			g_message("Failed to resolve realpath for '%s'", s->device);
			realdev = g_strdup(s->device);
		}

		if (g_strcmp0(realdev, r_context()->bootslot) == 0) {
			booted = s;
			break;
		}
	}

	if (!booted) {
		gboolean extboot = FALSE;

		if (g_strcmp0(r_context()->bootslot, "/dev/nfs") == 0) {
			g_message("Detected nfs boot, ignoring missing active slot");
			extboot = TRUE;
		}

		if (g_strcmp0(r_context()->bootslot, "_external_") == 0) {
			g_message("Detected explicit external boot, ignoring missing active slot");
			extboot = TRUE;
		}

		if (extboot) {
			RaucSlot *ext_dummy = NULL;
			/* mark all as inactive */
			g_debug("Marking all slots as 'inactive'");
			for (GList *l = slotlist; l != NULL; l = l->next) {
				RaucSlot *s = g_hash_table_lookup(r_context()->config->slots, l->data);
				g_assert_nonnull(s);

				s->state = ST_INACTIVE;
			}

			/* Create and add dummy external slot */
			ext_dummy = g_new0(RaucSlot, 1);
			ext_dummy->name = g_strdup("external");
			ext_dummy->sclass = g_strdup("external");
			ext_dummy->type = g_strdup("virtual");
			ext_dummy->device = g_strdup(r_context()->bootslot);
			ext_dummy->readonly = TRUE;
			ext_dummy->state = ST_BOOTED;

			g_hash_table_insert(r_context()->config->slots, (gchar*)ext_dummy->name, ext_dummy);

			r_context()->config->slot_states_determined = TRUE;

			return TRUE;
		}

		g_set_error(
				error,
				R_SLOT_ERROR,
				R_SLOT_ERROR_NO_SLOT_WITH_STATE_BOOTED,
				"Did not find booted slot (matching '%s')", r_context()->bootslot);
		return FALSE;
	}

	/* Determine active group members */
	for (GList *l = slotlist; l != NULL; l = l->next) {
		RaucSlot *s = g_hash_table_lookup(r_context()->config->slots, l->data);
		g_assert_nonnull(s);

		if (s == booted) {
			s->state = ST_BOOTED;
			g_debug("Found booted slot: %s on %s", s->name, s->device);
		} else if (s->parent && s->parent == booted) {
			s->state = ST_ACTIVE;
		} else {
			s->state = ST_INACTIVE;
		}
	}

	r_context()->config->slot_states_determined = TRUE;

	return TRUE;
}

gboolean determine_boot_states(GError **error)
{
	GHashTableIter iter;
	RaucSlot *slot;
	gboolean had_errors = FALSE;

	/* get boot state */
	g_hash_table_iter_init(&iter, r_context()->config->slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		GError *ierror = NULL;

		if (!slot->bootname)
			continue;

		if (!r_boot_get_state(slot, &slot->boot_good, &ierror)) {
			g_message("Failed to get boot state of %s: %s", slot->name, ierror->message);
			had_errors = TRUE;
		}
	}

	if (had_errors)
		g_set_error_literal(
				error,
				R_SLOT_ERROR,
				R_SLOT_ERROR_NO_SLOT_WITH_STATE_BOOTED,
				"Could not determine all boot states");

	return !had_errors;
}
