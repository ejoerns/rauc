#pragma once

#include <glib.h>

#include "slot.h"

#define RAUC_SLOT_PREFIX	"slot"

/**
 * Load a single slot status from a file into a pre-allocated status structure.
 * If a problem occurs this structure is left unmodified.
 *
 * @param filename file to load
 * @param slotstatus pointer to the pre-allocated structure going to store the slot status
 * @param error a GError, or NULL
 *
 * @return TRUE if the slot status was successfully loaded. FALSE if there were errors.
 */
gboolean r_slot_status_read(const gchar *filename, RaucSlotStatus *slotstatus, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Save slot status file.
 *
 * @param filename name of destination file
 * @param ss the slot status to save
 * @param error a GError, or NULL
 */
gboolean r_slot_status_write(const gchar *filename, RaucSlotStatus *ss, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Load slot status.
 *
 * Takes care to fill in slot status information into the designated component
 * of the slot data structure. If the user configured a global status file in
 * the system.conf they are read from this file. Otherwise mount the given slot,
 * read the status information from its local status file and unmount the slot
 * afterwards. If a problem occurs the stored slot status consists of default
 * values. Do nothing if the status information have already been loaded before.
 *
 * @param dest_slot Slot to load status information for
 */
void r_slot_status_load(RaucSlot *dest_slot);

/**
 * Save slot status.
 *
 * This persists the status information from the designated component of the
 * given slot data structure. If the user configured a global status file in the
 * system.conf they are written to this file. Otherwise mount the given slot,
 * transfer the status information to the local status file and unmount the slot
 * afterwards.
 *
 * @param dest_slot Slot to write status information for
 * @param error return location for a GError, or NULL
 *
 * @return TRUE if slot is not mountable or saving status succeeded, FALSE otherwise
 */
gboolean r_slot_status_save(RaucSlot *dest_slot, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

typedef struct {
	gchar *boot_id;
} RSystemState;

/**
 * Load global state from file.
 *
 * Note that filename and state are passed explicitly here since the method is
 * desinged to be called during context setup where we cannot access context,
 * yet.
 *
 * @param filename File name to load state from
 * @param state RGlobalState to update from file
 * @param[out] error Return location for a GError, or NULL
 *
 * @return
 */
gboolean r_system_state_load(const gchar *filename, RSystemState *state, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Save global state to file.
 *
 * @param filename File name to save state to
 * @param state RGlobalState to save
 * @param[out] error Return location for a GError, or NULL
 */
gboolean r_system_state_save(GError **error)
G_GNUC_WARN_UNUSED_RESULT;

/**
 * Free state.
 *
 * @param state RGlobalState to free
 */
void r_system_state_free(RSystemState *state);
