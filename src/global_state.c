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
