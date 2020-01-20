#include <stdio.h>
#include <locale.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <context.h>
#include <install.h>
#include <manifest.h>
#include <mount.h>

#include "builder.h"
#include "common.h"

GMainLoop *r_loop = NULL;

typedef struct {
	gchar *tmpdir;
	TestConfig *test_config;
	TestSystem *test_system;
	TestBundle *test_bundle;
} InstallFixture;

static void install_fixture_set_up_bundle(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* bundle_builder;
	BundleContent* bundle_content;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, TRUE);

	bundle_builder = manifest_builder_default();
	bundle_content = bundle_content_from_manifest_builder(bundle_builder);
	fixture->test_bundle = test_bundle_from_bundle_content(bundle_content);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_bundle_central_status(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* bundle_builder;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	/* use a global status file */
	set_global_status(system_builder);
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, TRUE);

	bundle_builder = manifest_builder_default();
	fixture->test_bundle = test_bundle_from_manifest_builder(bundle_builder);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_bundle_custom_handler(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* manifest_builder;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, FALSE);

	manifest_builder = manifest_builder_default();
	/* use a custom handler */
	set_custom_handler(manifest_builder);
	fixture->test_bundle = test_bundle_from_manifest_builder(manifest_builder);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_bundle_install_check_hook(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* bundle_builder;
	BundleContent* bundle_content;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, FALSE);

	bundle_builder = manifest_builder_default();
	/* use an install-check hook */
	add_install_hook(bundle_builder, "install-check");
	bundle_content = bundle_content_from_manifest_builder(bundle_builder);
	fixture->test_bundle = test_bundle_from_bundle_content(bundle_content);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_bundle_install_hook(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* bundle_builder;
	BundleContent* bundle_content;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, TRUE);

	bundle_builder = manifest_builder_default();
	/* use an install slot hook */
	add_slot_hook(bundle_builder, "rootfs", "install");
	add_slot_hook(bundle_builder, "appfs", "install");
	bundle_content = bundle_content_from_manifest_builder(bundle_builder);
	fixture->test_bundle = test_bundle_from_bundle_content(bundle_content);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_bundle_post_hook(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder* bundle_builder;
	BundleContent* bundle_content;

	if (!test_running_as_root())
		return;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, TRUE);

	bundle_builder = manifest_builder_default();
	/* use a post-install slot hook */
	add_slot_hook(bundle_builder, "rootfs", "post-install");
	add_slot_hook(bundle_builder, "appfs", "post-install");
	bundle_content = bundle_content_from_manifest_builder(bundle_builder);
	fixture->test_bundle = test_bundle_from_bundle_content(bundle_content);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_set_up_system_conf(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rescue.0", "factory0");
	add_boot_slot(system_builder, "rescue.1", "factory1");
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_boot_slot(system_builder, "rootfs.1", "system1");
	add_boot_slot(system_builder, "rootfs.2", "system2");
	add_child_slot(system_builder, "appfs.0", "rootfs.0");
	add_child_slot(system_builder, "appfs.1", "rootfs.1");
	add_child_slot(system_builder, "appfs.2", "rootfs.2");
	add_child_slot(system_builder, "demofs.0", "rootfs.0");
	add_child_slot(system_builder, "demofs.1", "rootfs.1");
	add_child_slot(system_builder, "demofs.2", "rootfs.2");
	add_slot(system_builder, "bootloader.0");
	add_slot(system_builder, "prebootloader.0");
	fixture->test_config = test_config_builder_end(system_builder);

	r_context_conf()->configpath = fixture->test_config->configpath;

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

static void install_fixture_tear_down(InstallFixture *fixture,
		gconstpointer user_data)
{

	cleanup_test_bundle(fixture->test_bundle);
	cleanup_test_system(fixture->test_system);
	cleanup_test_config(fixture->test_config);

	if (fixture->tmpdir)
		test_rm_tree(fixture->tmpdir, NULL);
}

static void install_test_bootname(InstallFixture *fixture,
		gconstpointer user_data)
{
	g_assert_nonnull(r_context()->bootslot);
}

static gboolean find_install_image(GList *images, const gchar *slotclass)
{
	for (GList *l = images; l != NULL; l = l->next) {
		RaucImage *image = l->data;

		if (g_strcmp0(image->slotclass, slotclass) == 0)
			return TRUE;
	}

	return FALSE;
}

static void install_test_target(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucManifest *rm = NULL;
	GHashTable *tgrp;
	GList *selected_images = NULL;
	GError *error = NULL;
	ManifestBuilder *manifest_builder;
	TestManifest *test_manifest;

	manifest_builder = manifest_builder_new();
	add_image(manifest_builder, "rootfs");
	add_image(manifest_builder, "appfs");
	add_image(manifest_builder, "demofs");
	add_image(manifest_builder, "bootloader");
	test_manifest = manifest_builder_end(manifest_builder, FALSE);

	g_assert_true(load_manifest_file(test_manifest->pathname, &rm, NULL));

	r_context_conf()->bootslot = g_strdup("system0");

	g_assert_true(determine_slot_states(NULL));

	g_assert_nonnull(r_context()->config);
	g_assert_nonnull(r_context()->config->slots);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rescue.0"))->state, ==, ST_INACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rootfs.0"))->state, ==, ST_BOOTED);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "rootfs.1"))->state, ==, ST_INACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "appfs.0"))->state, ==, ST_ACTIVE);
	g_assert_cmpint(((RaucSlot*) g_hash_table_lookup(r_context()->config->slots, "appfs.1"))->state, ==, ST_INACTIVE);

	tgrp = determine_target_install_group();

	g_assert_nonnull(tgrp);

	g_assert_true(g_hash_table_contains(tgrp, "rescue"));
	g_assert_true(g_hash_table_contains(tgrp, "rootfs"));
	g_assert_true(g_hash_table_contains(tgrp, "appfs"));
	g_assert_true(g_hash_table_contains(tgrp, "demofs"));
	g_assert_true(g_hash_table_contains(tgrp, "bootloader"));
	g_assert_true(g_hash_table_contains(tgrp, "prebootloader"));
	//Deactivated check as the actual behavior is GHashTable-implementation-defined
	//g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rescue"))->name, ==, "rescue.0");
	/* We need to assure that the algorithm did not select the active group '0' */
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, !=, "rootfs.0");
	/* The algorithm could select either group '1' or group '2'. The actual
	 * selection is still GHashTable-implementation-defined.*/
	if (g_strcmp0(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, "rootfs.1") == 0) {
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, ==, "rootfs.1");
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "appfs"))->name, ==, "appfs.1");
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "demofs"))->name, ==, "demofs.1");
	} else {
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, ==, "rootfs.2");
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "appfs"))->name, ==, "appfs.2");
		g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "demofs"))->name, ==, "demofs.2");
	}
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "bootloader"))->name, ==, "bootloader.0");
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "prebootloader"))->name, ==, "prebootloader.0");
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 6);

	selected_images = get_install_images(rm, tgrp, &error);
	g_assert_nonnull(selected_images);
	g_assert_no_error(error);

	g_assert_cmpint(g_list_length(selected_images), ==, 4);

	g_assert_true(find_install_image(selected_images, "rootfs"));
	g_assert_true(find_install_image(selected_images, "appfs"));
	g_assert_true(find_install_image(selected_images, "demofs"));
	g_assert_true(find_install_image(selected_images, "bootloader"));

	g_hash_table_unref(tgrp);
}

/* Test with image for non-redundant active target slot. */
static void test_install_determine_target_group_non_redundant(void)
{
	GHashTable *tgrp = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system0");
	r_context();

	g_assert_true(determine_slot_states(NULL));

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* We must not have any updatable slot detected */
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 0);

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

/* Test a typical asynchronous slot setup (rootfs + rescuefs) with additional
 * childs */
static void test_install_target_group_async(void)
{
	GHashTable *tgrp = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rescue.0", "rescue");
	add_child_slot(system_builder, "rescueapp.0", "rescue.0");
	add_boot_slot(system_builder, "rootfs.0", "system");
	add_child_slot(system_builder, "appfs.0", "rootfs.0");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("rescue");
	r_context();

	g_assert_true(determine_slot_states(NULL));

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* Rootfs must be in target group, rescue not */
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 2);
	g_assert_true(g_hash_table_contains(tgrp, "rootfs"));
	g_assert_true(g_hash_table_contains(tgrp, "appfs"));

	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, ==, "rootfs.0");
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "appfs"))->name, ==, "appfs.0");

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

/* Test a typical synchronous slot setup (rootfs a + b) with appfs childs */
static void test_install_target_group_sync(void)
{
	GHashTable *tgrp = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system1");
	r_context();

	g_assert_true(determine_slot_states(NULL));

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* First rootfs.0 and appfs.0 must be in target group, other not */
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 2);
	g_assert_true(g_hash_table_contains(tgrp, "rootfs"));
	g_assert_true(g_hash_table_contains(tgrp, "appfs"));

	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, ==, "rootfs.0");
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "appfs"))->name, ==, "appfs.0");

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

/* Test with extra loose (non-booted) groups in parent child relation */
static void test_install_target_group_loose(void)
{
	GHashTable *tgrp = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_slot(system_builder, "xloader.0");
	add_child_slot(system_builder, "bootloader.0", "xloader.0");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system0");
	r_context();

	g_assert_true(determine_slot_states(NULL));

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* Rootfs must be in target group, rescue not */
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 2);
	g_assert_true(g_hash_table_contains(tgrp, "xloader"));
	g_assert_true(g_hash_table_contains(tgrp, "bootloader"));

	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "xloader"))->name, ==, "xloader.0");
	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "bootloader"))->name, ==, "bootloader.0");

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

/* Test with 3 redundant slots */
static void test_install_target_group_n_redundant(void)
{
	GHashTable *tgrp = NULL;

	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_boot_slot(system_builder, "rootfs.1", "system1");
	add_boot_slot(system_builder, "rootfs.2", "system2");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system1");
	r_context();

	g_assert_true(determine_slot_states(NULL));

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* Rootfs must be in target group, rescue not */
	g_assert_cmpint(g_hash_table_size(tgrp), ==, 1);
	g_assert_true(g_hash_table_contains(tgrp, "rootfs"));

	g_assert_cmpstr(((RaucSlot*)g_hash_table_lookup(tgrp, "rootfs"))->name, ==, "rootfs.0");

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

/* Test image selection, default redundancy setup */
static void test_install_image_selection(void)
{
	RaucManifest *rm = NULL;
	GHashTable *tgrp = NULL;
	GError *error = NULL;
	GList *selected_images = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder *manifest_builder;
	TestManifest *test_manifest;

	manifest_builder = manifest_builder_default();
	test_manifest = manifest_builder_end(manifest_builder, TRUE);

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_boot_slot(system_builder, "rootfs.1", "system1");
	add_child_slot(system_builder, "appfs.0", "rootfs.0");
	add_child_slot(system_builder, "appfs.1", "rootfs.1");
	add_slot(system_builder, "bootloader.0");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system1");
	r_context();

	load_manifest_mem(test_manifest->data, &rm, &error);
	g_assert_no_error(error);

	determine_slot_states(&error);
	g_assert_no_error(error);

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	selected_images = get_install_images(rm, tgrp, &error);
	g_assert_nonnull(selected_images);
	g_assert_no_error(error);

	/* We expecte the image selection to return both appfs.img and
	 * rootfs.img as we have matching slots for them. */
	g_assert_cmpint(g_list_length(selected_images), ==, 2);

	g_assert_true(find_install_image(selected_images, "rootfs"));
	g_assert_true(find_install_image(selected_images, "appfs"));

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

static void test_install_image_selection_no_matching_slot(void)
{
	RaucManifest *rm = NULL;
	GHashTable *tgrp = NULL;
	GError *error = NULL;
	GList *selected_images = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;
	ManifestBuilder *manifest_builder;
	TestManifest *test_manifest;

	manifest_builder = manifest_builder_default();
	test_manifest = manifest_builder_end(manifest_builder, TRUE);

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_boot_slot(system_builder, "rootfs.1", "system1");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system1");
	r_context();

	load_manifest_mem(test_manifest->data, &rm, &error);
	g_assert_no_error(error);

	determine_slot_states(&error);
	g_assert_no_error(error);

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* we expect the image mapping to fail as there is no slot candidate
	 * for image.appfs */
	selected_images = get_install_images(rm, tgrp, &error);
	g_assert_null(selected_images);
	g_assert_error(error, R_INSTALL_ERROR, R_INSTALL_ERROR_FAILED);

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

static void test_install_image_readonly(void)
{
	GBytes *data = NULL;
	RaucManifest *rm = NULL;
	GHashTable *tgrp = NULL;
	GError *error = NULL;
	GList *selected_images = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

#define MANIFEST "\
[update]\n\
compatible=foo\n\
\n\
[image.rescuefs]\n\
filename=rootfs.img\n\
"

	system_builder = test_config_builder_new();
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_slot(system_builder, "rescuefs.0");
	set_slot_readonly(system_builder, "rescuefs.0");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system0");
	r_context();

	data = g_bytes_new_static(MANIFEST, sizeof(MANIFEST));
	load_manifest_mem(data, &rm, &error);
	g_assert_no_error(error);

	determine_slot_states(&error);
	g_assert_no_error(error);

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* we expect the image mapping to fail as there is an image for a
	 * readonly slot */
	selected_images = get_install_images(rm, tgrp, &error);
	g_assert_null(selected_images);
	g_assert_error(error, R_INSTALL_ERROR, R_INSTALL_ERROR_FAILED);

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}


static void test_install_image_variants(void)
{
	GBytes *data = NULL;
	RaucManifest *rm = NULL;
	GHashTable *tgrp = NULL;
	GList *install_images = NULL;
	RaucImage *test_img = NULL;
	GError *error = NULL;
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

#define MANIFEST_VARIANT "\
[update]\n\
compatible=foo\n\
\n\
[image.rootfs.variant-1]\n\
filename=dummy\n\
\n\
[image.rootfs]\n\
filename=dummy\n\
"

#define MANIFEST_DEFAULT_VARIANT "\
[update]\n\
compatible=foo\n\
\n\
[image.rootfs]\n\
filename=dummy\n\
"

#define MANIFEST_OTHER_VARIANT "\
[update]\n\
compatible=foo\n\
\n\
[image.rootfs.variant-2]\n\
filename=dummy\n\
"

	system_builder = test_config_builder_new();
	test_config_set_variant_name(system_builder, "variant-1");
	add_boot_slot(system_builder, "rootfs.0", "system0");
	add_boot_slot(system_builder, "rootfs.1", "system1");
	test_config = test_config_builder_end(system_builder);

	/* Set up context */
	r_context_conf()->configpath = test_config->configpath;
	r_context_conf()->bootslot = g_strdup("system1");
	r_context();

	determine_slot_states(&error);
	g_assert_no_error(error);

	tgrp = determine_target_install_group();
	g_assert_nonnull(tgrp);

	/* Test with manifest containing default and specific variant */
	data = g_bytes_new_static(MANIFEST_VARIANT, sizeof(MANIFEST_VARIANT));
	load_manifest_mem(data, &rm, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rm);

	install_images = get_install_images(rm, tgrp, NULL);
	g_assert_nonnull(install_images);
	g_clear_pointer(&rm, free_manifest);

	g_assert_cmpint(g_list_length(install_images), ==, 1);

	test_img = (RaucImage*)g_list_nth_data(install_images, 0);
	g_assert_nonnull(test_img);
	g_assert_cmpstr(test_img->variant, ==, "variant-1");

	/* Test with manifest containing only default variant */
	data = g_bytes_new_static(MANIFEST_DEFAULT_VARIANT, sizeof(MANIFEST_DEFAULT_VARIANT));
	load_manifest_mem(data, &rm, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rm);

	install_images = get_install_images(rm, tgrp, NULL);
	g_assert_nonnull(install_images);
	g_clear_pointer(&rm, free_manifest);

	g_assert_cmpint(g_list_length(install_images), ==, 1);

	test_img = (RaucImage*)g_list_nth_data(install_images, 0);
	g_assert_nonnull(test_img);
	g_assert_null(test_img->variant);

	/* Test with manifest containing only non-matching specific variant (must fail) */
	data = g_bytes_new_static(MANIFEST_OTHER_VARIANT, sizeof(MANIFEST_OTHER_VARIANT));
	load_manifest_mem(data, &rm, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rm);

	install_images = get_install_images(rm, tgrp, NULL);
	g_assert_null(install_images);
	g_clear_pointer(&rm, free_manifest);

	g_hash_table_unref(tgrp);
	cleanup_test_config(test_config);
}

static gboolean r_quit(gpointer data)
{
	g_assert_nonnull(r_loop);
	g_main_loop_quit(r_loop);

	return G_SOURCE_REMOVE;
}

static gboolean install_notify(gpointer data)
{
	RaucInstallArgs *args = data;

	g_assert_nonnull(args);

	return G_SOURCE_REMOVE;
}

static gboolean install_cleanup(gpointer data)
{
	RaucInstallArgs *args = data;

	g_assert_nonnull(args);
	g_assert_cmpint(args->status_result, ==, 0);
	g_assert_false(g_queue_is_empty(&args->status_messages));

	g_queue_clear(&args->status_messages);
	install_args_free(args);

	g_idle_add(r_quit, NULL);

	return G_SOURCE_REMOVE;
}

static void install_test_bundle(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *mountprefix, *slotfile, *testfilepath, *mountdir;
	RaucInstallArgs *args;
	GError *ierror = NULL;
	gboolean res;

	/* needs to run as root */
	if (!test_running_as_root())
		return;

	/* Set mount path to current temp dir */
	mountprefix = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountprefix);
	r_context_conf()->mountprefix = mountprefix;
	r_context();

	args = install_args_new();
	args->name = g_strdup(fixture->test_bundle->bundlepath);
	args->notify = install_notify;
	args->cleanup = install_cleanup;
	res = do_install_bundle(args, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	slotfile = g_build_filename(fixture->test_system->tmpdir, "slots/rootfs.1.device", NULL);
	mountdir = g_build_filename(fixture->tmpdir, "mnt", NULL);
	g_assert(test_mkdir_relative(fixture->tmpdir, "mnt", 0777) == 0);
	testfilepath = g_build_filename(mountdir, "verify.txt", NULL);
	g_assert(test_mount(slotfile, mountdir));
	g_assert(g_file_test(testfilepath, G_FILE_TEST_IS_REGULAR));
	g_assert(test_umount(fixture->tmpdir, "mnt"));

	args->status_result = 0;

	g_free(slotfile);
	g_free(mountdir);
	g_free(testfilepath);
}

static void install_test_bundle_thread(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucInstallArgs *args = install_args_new();
	gchar *mountdir;

	/* needs to run as root */
	if (!test_running_as_root())
		return;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = mountdir;
	r_context();

	args->name = g_strdup(fixture->test_bundle->bundlepath);
	args->notify = install_notify;
	args->cleanup = install_cleanup;

	r_loop = g_main_loop_new(NULL, FALSE);
	g_assert_true(install_run(args));
	g_main_loop_run(r_loop);
	g_clear_pointer(&r_loop, g_main_loop_unref);
}

static void install_test_bundle_hook_install_check(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *mountdir;
	RaucInstallArgs *args;
	GError *ierror = NULL;

	/* needs to run as root */
	if (!test_running_as_root())
		return;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = mountdir;
	r_context();

	args = install_args_new();
	args->name = g_strdup(fixture->test_bundle->bundlepath);
	args->notify = install_notify;
	args->cleanup = install_cleanup;
	g_assert_false(do_install_bundle(args, &ierror));
	g_assert_cmpstr(ierror->message, ==, "Installation error: Bundle rejected: Hook returned: No, I won't install this!");

	args->status_result = 0;

	g_free(mountdir);
}

static void install_test_bundle_hook_install(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *mountdir, *slotfile, *stamppath, *hookfilepath;
	RaucInstallArgs *args;
	GError *ierror = NULL;
	gboolean res = FALSE;

	/* needs to run as root */
	if (!test_running_as_root())
		return;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = mountdir;
	r_context();

	args = install_args_new();
	args->name = g_strdup(fixture->test_bundle->bundlepath);
	args->notify = install_notify;
	args->cleanup = install_cleanup;
	res = do_install_bundle(args, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	slotfile = g_build_filename(fixture->test_system->tmpdir, "slots/rootfs.1.device", NULL);
	hookfilepath = g_build_filename(mountdir, "hook-install", NULL);
	stamppath = g_build_filename(mountdir, "hook-stamp", NULL);
	g_assert(test_mount(slotfile, mountdir));
	g_assert_true(g_file_test(hookfilepath, G_FILE_TEST_IS_REGULAR));
	g_assert_false(g_file_test(stamppath, G_FILE_TEST_IS_REGULAR));
	g_assert(test_umount(fixture->tmpdir, "mount"));
	g_free(hookfilepath);
	g_free(stamppath);
	g_free(slotfile);

	slotfile = g_build_filename(fixture->test_system->tmpdir, "slots/appfs.1.device", NULL);
	stamppath = g_build_filename(mountdir, "hook-stamp", NULL);
	g_assert(test_mount(slotfile, mountdir));
	g_assert_false(g_file_test(stamppath, G_FILE_TEST_IS_REGULAR));
	g_assert(test_umount(fixture->tmpdir, "mount"));
	g_free(stamppath);
	g_free(slotfile);

	args->status_result = 0;
}

static void install_test_bundle_hook_post_install(InstallFixture *fixture,
		gconstpointer user_data)
{
	gchar *mountdir, *slotfile, *testfilepath, *stamppath;
	RaucInstallArgs *args;

	/* needs to run as root */
	if (!test_running_as_root())
		return;

	/* Set mount path to current temp dir */
	mountdir = g_build_filename(fixture->tmpdir, "mount", NULL);
	g_assert_nonnull(mountdir);
	r_context_conf()->mountprefix = mountdir;
	r_context();

	args = install_args_new();
	args->name = g_strdup(fixture->test_bundle->bundlepath);
	args->notify = install_notify;
	args->cleanup = install_cleanup;
	g_assert_true(do_install_bundle(args, NULL));

	slotfile = g_build_filename(fixture->test_system->tmpdir, "slots/rootfs.1.device", NULL);
	testfilepath = g_build_filename(mountdir, "verify.txt", NULL);
	stamppath = g_build_filename(mountdir, "hook-stamp", NULL);
	g_assert(test_mount(slotfile, mountdir));
	g_assert(g_file_test(testfilepath, G_FILE_TEST_IS_REGULAR));
	g_assert(g_file_test(stamppath, G_FILE_TEST_IS_REGULAR));
	g_assert(test_umount(fixture->tmpdir, "mount"));
	g_free(stamppath);
	g_free(slotfile);
	g_free(testfilepath);

	slotfile = g_build_filename(fixture->test_system->tmpdir, "slots/appfs.1.device", NULL);
	stamppath = g_build_filename(mountdir, "hook-stamp", NULL);
	g_assert(test_mount(slotfile, mountdir));
	g_assert(!g_file_test(stamppath, G_FILE_TEST_IS_REGULAR));
	g_assert(test_umount(fixture->tmpdir, "mount"));
	g_free(stamppath);
	g_free(slotfile);

	args->status_result = 0;
}

static void install_fixture_set_up_system_user(InstallFixture *fixture,
		gconstpointer user_data)
{
	RaucSystemBuilder *system_builder;
	TestConfig *test_config;

	system_builder = test_config_builder_default();
	test_config = test_config_builder_end(system_builder);
	fixture->test_system = test_system_from_test_config(test_config, FALSE);

	fixture->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);
}

int main(int argc, char *argv[])
{
	gchar *path;
	setlocale(LC_ALL, "C");

	path = g_strdup_printf("%s:%s", g_getenv("PATH"), "test/bin");
	g_setenv("PATH", path, TRUE);
	g_free(path);

	g_test_init(&argc, &argv, NULL);

	g_test_add("/install/bootname", InstallFixture, NULL,
			install_fixture_set_up_system_user, install_test_bootname,
			install_fixture_tear_down);

	g_test_add("/install/target", InstallFixture, NULL,
			install_fixture_set_up_system_conf, install_test_target,
			install_fixture_tear_down);

	g_test_add_func("/install/target-group/non-redundant", test_install_determine_target_group_non_redundant);

	g_test_add_func("/install/target-group/async", test_install_target_group_async);

	g_test_add_func("/install/target-group/sync", test_install_target_group_sync);

	g_test_add_func("/install/target-group/loose", test_install_target_group_loose);

	g_test_add_func("/install/target-group/n-redundant", test_install_target_group_n_redundant);

	g_test_add_func("/install/image-selection/redundant", test_install_image_selection);

	g_test_add_func("/install/image-selection/non-matching", test_install_image_selection_no_matching_slot);

	g_test_add_func("/install/image-selection/readonly", test_install_image_readonly);

	g_test_add_func("/install/image-mapping/variants", test_install_image_variants);

	g_test_add("/install/bundle", InstallFixture, NULL,
			install_fixture_set_up_bundle, install_test_bundle,
			install_fixture_tear_down);

	g_test_add("/install/bundle/central-status", InstallFixture, NULL,
			install_fixture_set_up_bundle_central_status, install_test_bundle,
			install_fixture_tear_down);

	g_test_add("/install/bundle-thread", InstallFixture, NULL,
			install_fixture_set_up_bundle, install_test_bundle_thread,
			install_fixture_tear_down);

	g_test_add("/install/bundle-custom-handler", InstallFixture, NULL,
			install_fixture_set_up_bundle_custom_handler, install_test_bundle,
			install_fixture_tear_down);

	g_test_add("/install/bundle-hook/install-check", InstallFixture, NULL,
			install_fixture_set_up_bundle_install_check_hook, install_test_bundle_hook_install_check,
			install_fixture_tear_down);

	g_test_add("/install/bundle-hook/slot-install", InstallFixture, NULL,
			install_fixture_set_up_bundle_install_hook, install_test_bundle_hook_install,
			install_fixture_tear_down);

	g_test_add("/install/bundle-hook/slot-post-install", InstallFixture, NULL,
			install_fixture_set_up_bundle_post_hook, install_test_bundle_hook_post_install,
			install_fixture_tear_down);
	return g_test_run();
}
