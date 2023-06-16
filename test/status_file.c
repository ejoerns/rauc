#include <glib.h>
#include <locale.h>

#include "common.h"
#include "context.h"
#include "config.h"
#include "status_file.h"

typedef struct {
	gchar *tmpdir;
} ConfigFileFixture;

static void config_file_fixture_set_up(ConfigFileFixture *fixture,
		gconstpointer user_data)
{
	fixture->tmpdir = g_dir_make_tmp("rauc-conf_file-XXXXXX", NULL);
	g_assert_nonnull(fixture->tmpdir);

	r_context_conf()->configpath = g_strdup("test/test.conf");
	r_context_conf()->handlerextra = g_strdup("--dummy1 --dummy2");
	r_context();
}

static void config_file_fixture_set_up_global(ConfigFileFixture *fixture,
		gconstpointer user_data)
{
	fixture->tmpdir = g_dir_make_tmp("rauc-conf_file-XXXXXX", NULL);
	g_assert_nonnull(fixture->tmpdir);

	r_context_conf()->configpath = g_strdup("test/test-global.conf");
	r_context();
}

static void config_file_fixture_tear_down(ConfigFileFixture *fixture,
		gconstpointer user_data)
{
	g_assert_true(rm_tree(fixture->tmpdir, NULL));
	g_free(fixture->tmpdir);
	r_context_clean();
}

static void config_file_statusfile_missing(ConfigFileFixture *fixture,
		gconstpointer user_data)
{
	g_autoptr(RaucConfig) config = NULL;
	GError *ierror = NULL;
	gboolean res;
	g_autofree gchar* pathname = NULL;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=barebox\n\
mountprefix=/mnt/myrauc/\n";


	pathname = write_tmp_file(fixture->tmpdir, "valid_bootloader.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	res = load_config(pathname, &config, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
	g_assert_nonnull(config);
	g_assert_nonnull(config->statusfile_path);
	g_assert_cmpstr(config->statusfile_path, ==, "per-slot");
}


static void config_file_test_read_slot_status(void)
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


static void config_file_test_write_slot_status(void)
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

static void config_file_test_global_slot_status(ConfigFileFixture *fixture,
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

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "C");

	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/config-file/read-slot-status", config_file_test_read_slot_status);
	g_test_add_func("/config-file/write-read-slot-status", config_file_test_write_slot_status);
	g_test_add("/config-file/statusfile-missing", ConfigFileFixture, NULL,
			config_file_fixture_set_up, config_file_statusfile_missing,
			config_file_fixture_tear_down);
	g_test_add("/config-file/global-slot-staus", ConfigFileFixture, NULL,
			config_file_fixture_set_up_global, config_file_test_global_slot_status,
			config_file_fixture_tear_down);

	return g_test_run();
}
