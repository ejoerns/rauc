#include "bootchooser.h"
#include "context.h"
#include "mark.h"

static RaucSlot* get_slot_by_identifier(const gchar *identifier, GError **error)
{
	GHashTableIter iter;
	RaucSlot *slot = NULL, *booted = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	g_hash_table_iter_init(&iter, r_context()->config->slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&booted)) {
		if (booted->state == ST_BOOTED)
			break;
		booted = NULL;
	}
	g_assert(booted);

	if (!g_strcmp0(identifier, "booted")) {
		slot = booted;
	} else if (!g_strcmp0(identifier, "other")) {
		g_hash_table_iter_init(&iter, r_context()->config->slots);
		while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&slot)) {
			if (slot->sclass == booted->sclass && !slot->parent && slot->bootname && slot != booted)
				break;
			slot = NULL;
		}
		if (!slot)
			g_set_error(error,
				    R_SLOT_ERROR,
				    R_SLOT_ERROR_FAILED,
				    "No other slot found");
	} else {
		g_hash_table_iter_init(&iter, r_context()->config->slots);
		while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&slot)) {
			if (slot->sclass == booted->sclass && !slot->parent && !g_strcmp0(slot->name, identifier))
				break;
			slot = NULL;
		}
		if (!slot)
			g_set_error(error,
				    R_SLOT_ERROR,
				    R_SLOT_ERROR_FAILED,
				    "No slot with class %s and name %s found",
				    booted->sclass,
				    identifier);
	}

	return slot;
}

gboolean mark_run(const gchar *state,
		  const gchar *slot_identifier,
		  gchar **slot_name,
		  gchar **message)
{
	RaucSlot *slot = NULL;
	GError *error = NULL;
	gboolean res;

	g_assert(slot_name == NULL || *slot_name == NULL);
	g_assert(message != NULL && *message == NULL);

	slot = get_slot_by_identifier(slot_identifier, &error);
	if (error) {
		res = FALSE;
		*message = g_strdup(error->message);
		g_error_free(error);
		goto out;
	}

	if (!g_strcmp0(state, "good")) {
		res = r_boot_set_state(slot, TRUE);
		*message = g_strdup_printf((res) ? "marked slot %s as good" : "failed to mark slot %s as good", slot->name);
	} else if (!g_strcmp0(state, "bad")) {
		res = r_boot_set_state(slot, FALSE);
		*message = g_strdup_printf((res) ? "marked slot %s as bad" : "failed to mark slot %s as bad", slot->name);
	} else if (!g_strcmp0(state, "active")) {
		res = r_boot_set_primary(slot);
		*message = g_strdup_printf((res) ? "activated slot %s" : "failed to activate slot %s", slot->name);
	} else {
		res = FALSE;
		*message = g_strdup_printf("unknown subcommand %s", state);
	}

out:
	if (res && slot_name)
		*slot_name = g_strdup(slot->name);

	return res;
}
