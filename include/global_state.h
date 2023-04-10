#pragma once

#include <glib.h>

#include "bundle.h"
#include "config_file.h"

typedef struct {
	gchar *boot_id;
} RGlobalState;

/**
 * Load global state from file.
 *
 * @param filename File name to load state from
 * @param state RGlobalState to update from file
 * @param[out] error Return location for a GError, or NULL
 *
 * @return
 */
gboolean r_global_state_load(const gchar *filename, RGlobalState *state, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Save global state to file.
 *
 * @param filename File name to save state to
 * @param state RGlobalState to save
 * @param[out] error Return location for a GError, or NULL
 */
gboolean r_global_state_save(const gchar *filename, RGlobalState *state, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Free state.
 *
 * @param state RGlobalState to free
 */
void r_global_state_free(RGlobalState *state);

/**
 * Determines the states (ACTIVE | INACTIVE | BOOTED) of the slots specified in
 * system configuration.
 *
 * @param error return location for a GError
 *
 * @return TRUE if succeeded, FALSE if failed
 */
gboolean determine_slot_states(GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Obtains boot status information for all relevant slots and stores
 * information into context.
 *
 * @param error return location for a GError
 *
 * @return TRUE if succeeded, FALSE if failed
 */
gboolean determine_boot_states(GError **error)
G_GNUC_WARN_UNUSED_RESULT;