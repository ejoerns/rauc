#include <glib.h>
#include <locale.h>

#include "common.h"
#include "context.h"
#include "config.h"
#include "status_file.h"

typedef struct {
	gchar *tmpdir;
} StatusFileFixture;

static void status_file_fixture_set_up_global(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	fixture->tmpdir = g_dir_make_tmp("rauc-conf_file-XXXXXX", NULL);
	g_assert_nonnull(fixture->tmpdir);

	replace_strdup(&r_context_conf()->configpath, "test/test-global.conf");
	r_context();
}

static void status_file_fixture_tear_down(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_assert_true(rm_tree(fixture->tmpdir, NULL));
	g_free(fixture->tmpdir);
	r_context_clean();
}


static void status_file_test_read_slot_status(void)
{
	GError *ierror = NULL;
	gboolean res;
	RaucSlotStatus *ss = g_new0(RaucSlotStatus, 1);
	res = r_slot_status_read("test/rootfs.raucs", ss, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
	g_assert_nonnull(ss);
	g_assert_cmpstr(ss->status, ==, "ok");
	g_assert_cmpint(ss->checksum.type, ==, G_CHECKSUM_SHA256);
	g_assert_cmpstr(ss->checksum.digest, ==,
			"e437ab217356ee47cd338be0ffe33a3cb6dc1ce679475ea59ff8a8f7f6242b27");

	r_slot_free_status(ss);
}


static void status_file_test_write_slot_status(void)
{
	RaucSlotStatus *ss = g_new0(RaucSlotStatus, 1);

	ss->status = g_strdup("ok");
	ss->checksum.type = G_CHECKSUM_SHA256;
	ss->checksum.digest = g_strdup("dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");

	g_assert_true(r_slot_status_write("test/savedslot.raucs", ss, NULL));

	r_slot_free_status(ss);
	ss = g_new0(RaucSlotStatus, 1);

	g_assert_true(r_slot_status_read("test/savedslot.raucs", ss, NULL));

	g_assert_nonnull(ss);
	g_assert_cmpstr(ss->status, ==, "ok");
	g_assert_cmpint(ss->checksum.type, ==, G_CHECKSUM_SHA256);
	g_assert_cmpstr(ss->checksum.digest, ==,
			"dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");

	r_slot_free_status(ss);
}

static void status_file_test_global_slot_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	GHashTable *slots = r_context()->config->slots;
	GHashTableIter iter;
	GError *ierror = NULL;
	RaucSlot *slot;
	gboolean res;

	g_assert_nonnull(r_context()->config->statusfile_path);

	/* Set status for all slots */
	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		if (slot->status)
			r_slot_free_status(slot->status);

		g_debug("Set default status for slot %s.", slot->name);
		slot->status = g_new0(RaucSlotStatus, 1);
		slot->status->status = g_strdup("ok");
		slot->status->checksum.type = G_CHECKSUM_SHA256;
		slot->status->checksum.digest = g_strdup("dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");
	}

	/* Save status for all slots */
	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		res = r_slot_status_save(slot, &ierror);
		g_assert_no_error(ierror);
		g_assert_true(res);
	}

	/* Clear status for all slots */
	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		if (slot->status)
			r_slot_free_status(slot->status);

		slot->status = NULL;
	}

	/* Check status for all slots */
	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		r_slot_status_load(slot);
		g_assert_nonnull(slot->status);
		g_assert_cmpstr(slot->status->status, ==, "ok");
		g_assert_cmpint(slot->status->checksum.type, ==, G_CHECKSUM_SHA256);
		g_assert_cmpstr(slot->status->checksum.digest, ==,
				"dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");
	}
}

/* Loads system status from a file containing only system status information */
static void status_file_test_load_system_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	GError *ierror = NULL;
	gboolean res;
	g_autoptr(RSystemStatus) system_status = NULL;

	const gchar *status_file = "\
[system]\n\
boot-id=924ebd2e-c85f-4c48-b92d-cd1b378d9994\n\
";
	pathname = write_tmp_file(fixture->tmpdir, "system_only.raucs", status_file, NULL);
	g_assert_nonnull(pathname);

	system_status = g_new0(RSystemStatus, 1);
	g_assert_nonnull(system_status);

	/* assert error-free loading */
	res = r_system_status_load(pathname, system_status, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert loaded data */
	g_assert_cmpstr(system_status->boot_id, ==, "924ebd2e-c85f-4c48-b92d-cd1b378d9994");
}

/* Loads a broken system status */
static void status_file_test_load_broken(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	g_autoptr(GError) ierror = NULL;
	gboolean res;
	g_autoptr(RSystemStatus) system_status = NULL;

	const gchar *status_file = "\
[system]\n\
boot-id=924ebd2e-c85f-4c48-b92d-cd1b378d9994\n\
\n\
[broken\n\
";
	pathname = write_tmp_file(fixture->tmpdir, "broken_status.raucs", status_file, NULL);
	g_assert_nonnull(pathname);

	system_status = g_new0(RSystemStatus, 1);
	g_assert_nonnull(system_status);

	/* assert parsing error during loading */
	res = r_system_status_load(pathname, system_status, &ierror);
	g_assert_false(res);
	g_assert_error(ierror, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE);
}

/* Creates and saves system status (verify by loading again) */
static void status_file_test_save_system_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	g_autoptr(GError) ierror = NULL;
	gboolean res;
	g_autoptr(RSystemStatus) system_status = NULL;

	pathname = g_build_filename(fixture->tmpdir, "system_only.raucs", NULL);
	g_assert_nonnull(pathname);

	replace_strdup(&r_context()->config->statusfile_path, pathname);
	replace_strdup(&r_context()->system_status->boot_id, "e02a2afe-cf45-4d50-a3f3-c223ca0f480a");

	/* assert error-free saving*/
	res = r_system_status_save(&ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	system_status = g_new0(RSystemStatus, 1);
	g_assert_nonnull(system_status);

	/* re-load to verify content */
	res = r_system_status_load(pathname, system_status, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert loaded data */
	g_assert_cmpstr(system_status->boot_id, ==, "e02a2afe-cf45-4d50-a3f3-c223ca0f480a");
}

/* Creates and attempts to saves system status with per-slot statusfile configured
 * This is expected to be a noop. */
static void status_file_test_save_system_status_per_slot(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autoptr(GError) ierror = NULL;
	gboolean res;

	replace_strdup(&r_context()->config->statusfile_path, "per-slot");
	replace_strdup(&r_context()->system_status->boot_id, "e02a2afe-cf45-4d50-a3f3-c223ca0f480a");

	/* assert error-free saving (noop) */
	res = r_system_status_save(&ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert not having accidentally written a 'per-slot'-named status file */
	g_assert_false(g_file_test(r_context()->config->statusfile_path, G_FILE_TEST_EXISTS));
}

/* Creates and saves system status into file with existing slot status */
static void status_file_test_save_system_status_existing_slot_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	g_autoptr(GError) ierror = NULL;
	gboolean res;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_auto(GStrv) groups = NULL;
	gsize num_groups;

	const gchar *status_file = "\
[slot]\n\
status=ok\n\
sha256=e437ab217356ee47cd338be0ffe33a3cb6dc1ce679475ea59ff8a8f7f6242b27\n\
";
	pathname = write_tmp_file(fixture->tmpdir, "existing_slot_status.raucs", status_file, NULL);
	g_assert_nonnull(pathname);

	replace_strdup(&r_context()->config->statusfile_path, pathname);
	replace_strdup(&r_context()->system_status->boot_id, "e02a2afe-cf45-4d50-a3f3-c223ca0f480a");

	/* assert error-free saving*/
	res = r_system_status_save(&ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* re-load key file for asserting content */
	keyfile = g_key_file_new();
	res = g_key_file_load_from_file(keyfile, pathname, G_KEY_FILE_NONE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert loaded key file contains both existing [slot] and added [system] groups */
	groups = g_key_file_get_groups(keyfile, &num_groups);
	g_assert_cmpint(num_groups, ==, 2);
	g_assert_true(g_strv_contains((const gchar * const *)groups, "slot"));
	g_assert_true(g_strv_contains((const gchar * const *)groups, "system"));
}

/* Creates and saves system status into a broken existing status file */
static void status_file_test_save_system_status_broken_existing_slot_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	g_autoptr(GError) ierror = NULL;
	gboolean res;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_auto(GStrv) groups = NULL;
	gsize num_groups;

	const gchar *status_file = "\
[slot]\n\
status=ok\n\
sha256=e437ab217356ee47cd338be0ffe33a3cb6dc1ce679475ea59ff8a8f7f6242b27\n\
[broken\n\
";
	pathname = write_tmp_file(fixture->tmpdir, "existing_system_status.raucs", status_file, NULL);
	g_assert_nonnull(pathname);

	replace_strdup(&r_context()->config->statusfile_path, pathname);
	replace_strdup(&r_context()->system_status->boot_id, "e02a2afe-cf45-4d50-a3f3-c223ca0f480a");

	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "Failed to load status file: Key file contains line *");
	g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "Will move status file to * and re-create it.");

	/* assert error-free saving*/
	res = r_system_status_save(&ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* re-load key file for asserting content */
	keyfile = g_key_file_new();
	res = g_key_file_load_from_file(keyfile, pathname, G_KEY_FILE_NONE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert loaded key file contains only newly added [system] group */
	groups = g_key_file_get_groups(keyfile, &num_groups);
	g_assert_cmpint(num_groups, ==, 1);
	g_assert_true(g_strv_contains((const gchar * const *)groups, "system"));
}

/* Creates and saves slot status into file with existing system status */
static void status_file_test_save_slot_status_existing_system_status(StatusFileFixture *fixture,
		gconstpointer user_data)
{
	g_autofree gchar* pathname = NULL;
	RaucSlot *slot = NULL;
	GError *ierror = NULL;
	gboolean res;
	g_autoptr(GKeyFile) keyfile = NULL;
	g_auto(GStrv) groups = NULL;
	gsize num_groups;
	g_autofree gchar* checksum = NULL;

	const gchar *status_file = "\
[system]\n\
boot-id=e02a2afe-cf45-4d50-a3f3-c223ca0f480a\n\
";
	pathname = write_tmp_file(fixture->tmpdir, "existing_system_status.raucs", status_file, NULL);
	g_assert_nonnull(pathname);

	replace_strdup(&r_context()->config->statusfile_path, pathname);

	slot = g_hash_table_lookup(r_context()->config->slots, "rootfs.0");

	slot->status = g_new0(RaucSlotStatus, 1);
	slot->status->status = g_strdup("ok");
	slot->status->checksum.type = G_CHECKSUM_SHA256;
	slot->status->checksum.digest = g_strdup("dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");

	/* assert error-free saving*/
	res = r_slot_status_save(slot, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* re-load key file for asserting content */
	keyfile = g_key_file_new();
	res = g_key_file_load_from_file(keyfile, pathname, G_KEY_FILE_NONE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* assert loaded key file contains both existing [system] group and saved slot status group*/
	groups = g_key_file_get_groups(keyfile, &num_groups);
	g_message("%s", g_strjoinv(",", groups));
	g_assert_cmpint(num_groups, ==, 6); // Will also load defaults for slots
	g_assert_true(g_strv_contains((const gchar * const *)groups, "system"));
	g_assert_true(g_strv_contains((const gchar * const *)groups, "slot.rootfs.0"));
	checksum = g_key_file_get_string(keyfile, "slot.rootfs.0", "sha256", &ierror);
	g_assert_no_error(ierror);
	g_assert_cmpstr(checksum, ==, "dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551");
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "C");

	g_test_init(&argc, &argv, NULL);
	/* Tests for slot status only */
	g_test_add_func("/status-file/slot-status/read", status_file_test_read_slot_status);
	g_test_add_func("/status-file/slot_status/write-read", status_file_test_write_slot_status);
	g_test_add("/status-file/slot-status/global", StatusFileFixture, NULL,
			status_file_fixture_set_up_global, status_file_test_global_slot_status,
			status_file_fixture_tear_down);
	/* Tests for system status only */
	g_test_add("/status-file/system-status/load", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_load_system_status,
			status_file_fixture_tear_down);
	g_test_add("/status-file/system-status/load-broken", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_load_broken,
			status_file_fixture_tear_down);
	g_test_add("/status-file/system-status/save", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_save_system_status,
			status_file_fixture_tear_down);
	g_test_add("/status-file/system-status/save-per-slot", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_save_system_status_per_slot,
			status_file_fixture_tear_down);
	/* Combined tests */
	g_test_add("/status-file/combined/save-system-status-existing-slot-status", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_save_system_status_existing_slot_status,
			status_file_fixture_tear_down);
	g_test_add("/status-file/combined/save-system-status-broken-existing-slot-status", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_save_system_status_broken_existing_slot_status,
			status_file_fixture_tear_down);
	g_test_add("/status-file/combined/save-slot-status-existing-system-status", StatusFileFixture, NULL,
			status_file_fixture_set_up_global,
			status_file_test_save_slot_status_existing_system_status,
			status_file_fixture_tear_down);

	return g_test_run();
}
