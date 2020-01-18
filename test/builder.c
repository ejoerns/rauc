#include <glib.h>

#include <slot.h>
#include <context.h>
#include <mount.h>
#include <utils.h>

#include "builder.h"
#include "common.h"

RaucSystemBuilder* test_config_builder_new(void)
{
	RaucSystemBuilder *builder = g_new0(RaucSystemBuilder, 1);

	builder->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);

	builder->compatible = g_strdup("Test Config");

	builder->keyring = g_strdup("ca.cert.pem");

	builder->slots = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, r_slot_free);

	return builder;
}

void test_config_set_compatible(RaucSystemBuilder *builder, const gchar *compatible)
{
	g_free(builder->compatible);
	builder->compatible = g_strdup(compatible);
}

void test_config_set_variant_name(RaucSystemBuilder *builder, const gchar *variant)
{
	builder->variant_name = g_strdup(variant);
}

TestConfig* test_config_builder_end(RaucSystemBuilder *builder)
{
	GString *system_conf = g_string_new("");
	GHashTableIter iter;
	RaucSlot *iterslot = NULL;

	g_string_append(system_conf, "[system]\n");
	g_string_append_printf(system_conf, "compatible=%s\n", builder->compatible);
	g_string_append_printf(system_conf, "bootloader=%s\n", "grub");
	g_string_append_printf(system_conf, "grubenv=%s\n", "grubenv.test");
	if (builder->status_global)
		g_string_append_printf(system_conf, "statusfile=%s\n", "global.status");
	if (builder->variant_name)
		g_string_append_printf(system_conf, "variant-name=%s\n", builder->variant_name);
	g_string_append_c(system_conf, '\n');

	g_string_append(system_conf, "[keyring]\n");
	g_string_append_printf(system_conf, "path=%s\n", builder->keyring);
	g_string_append_c(system_conf, '\n');

	g_hash_table_iter_init(&iter, builder->slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &iterslot)) {
		g_string_append_printf(system_conf, "[slot.%s]\n", iterslot->name);
		g_string_append_printf(system_conf, "device=%s\n", iterslot->device);
		g_string_append_printf(system_conf, "type=%s\n", iterslot->type);
		if (iterslot->bootname)
			g_string_append_printf(system_conf, "bootname=%s\n", iterslot->bootname);
		if (iterslot->parent)
			g_string_append_printf(system_conf, "parent=%s\n", iterslot->parent->name);
		if (iterslot->readonly)
			g_string_append_printf(system_conf, "readonly=true");
		g_string_append_c(system_conf, '\n');
	}

	write_tmp_file(builder->tmpdir, "test.conf", g_string_free(system_conf, FALSE), NULL);
	builder->configpath = g_build_filename(builder->tmpdir, "test.conf", NULL);

	return builder;
}

void cleanup_test_config(TestConfig *test_config)
{
	if (!test_config)
		return;

	rm_tree(test_config->tmpdir, NULL);
}

TestSystem* test_system_from_test_config(TestConfig *builder, gboolean root)
{
	g_autofree gchar *certpath;
	GHashTableIter iter;
	RaucSlot *iterslot = NULL;
	TestSystem *return_system;

	r_context_conf()->configpath = builder->configpath;

	/* copy keyring file from default */
	certpath = g_build_filename(builder->tmpdir, builder->keyring, NULL);
	g_assert_nonnull(certpath);
	g_assert_true(test_copy_file("test/openssl-ca/dev-ca.pem", NULL, certpath, NULL));

	/* Setup pseudo devices */
	g_assert(test_mkdir_relative(builder->tmpdir, "slots", 0777) == 0);

	g_hash_table_iter_init(&iter, builder->slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &iterslot)) {
		g_assert(test_prepare_dummy_file(builder->tmpdir, iterslot->device,
				SLOT_SIZE, "/dev/zero") == 0);
		if (g_strcmp0(iterslot->type, "ext4") == 0)
			g_assert_true(test_make_filesystem(builder->tmpdir, iterslot->device));
		if (root)
			test_make_slot_user_writable(builder->tmpdir, iterslot->device);
	}

	/* Set dummy bootname provider */
	r_context_conf()->bootslot = g_strdup("system0");

	return_system = g_new0(TestSystem, 1);
	return_system->tmpdir = g_strdup(builder->tmpdir);

	return return_system;
}

void cleanup_test_system(TestSystem *system)
{
	if (!system)
		return;

	rm_tree(system->tmpdir, NULL);
}

void add_slot(RaucSystemBuilder *builder, const gchar *slotname)
{
	RaucSlot *slot = g_new0(RaucSlot, 1);

	slot->name = g_strdup(slotname);
	slot->device = g_strdup_printf("slots/%s.device", slotname);
	slot->type = g_strdup("ext4");

	g_hash_table_insert(builder->slots, g_strdup(slotname), slot);
}

void add_boot_slot(RaucSystemBuilder *builder, const gchar *slotname, const gchar *bootname)
{
	RaucSlot *slot = g_new0(RaucSlot, 1);

	slot->name = g_strdup(slotname);
	slot->device = g_strdup_printf("slots/%s.device", slotname);
	slot->type = g_strdup("ext4");
	slot->bootname = g_strdup(bootname);

	g_hash_table_insert(builder->slots, g_strdup(slotname), slot);
}

void add_child_slot(RaucSystemBuilder *builder, const gchar *slotname, const gchar *parent)
{
	RaucSlot *slot = g_new0(RaucSlot, 1);

	slot->name = g_strdup(slotname);
	slot->device = g_strdup_printf("slots/%s.device", slotname);
	slot->type = g_strdup("ext4");
	slot->parent = g_hash_table_lookup(builder->slots, parent);
	g_assert_nonnull(slot->parent);

	g_hash_table_insert(builder->slots, g_strdup(slotname), slot);
}

void set_slot_readonly(RaucSystemBuilder *builder, const gchar *slotname)
{
	RaucSlot *slot = g_hash_table_lookup(builder->slots, slotname);
	slot->readonly = TRUE;
}

void set_global_status(RaucSystemBuilder *builder)
{
	builder->status_global = TRUE;
}

void add_handler(RaucSystemBuilder *builder, const gchar *name, const gchar *content)
{
	// TODO
}


typedef struct {
	gchar* slotclass;
	gchar* filename;
	GList* files;
	GList* hooks;
} TestImage;

ManifestBuilder* manifest_builder_new(void)
{
	ManifestBuilder *builder = g_new0(ManifestBuilder, 1);

	builder->tmpdir = g_dir_make_tmp("rauc-XXXXXX", NULL);

	builder->compatible = g_strdup("Test Config");
	builder->default_ext = g_strdup("ext4");

	builder->images = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, r_free_image);

	return builder;
}

void set_default_ext(ManifestBuilder *builder, gchar *ext)
{
	g_free(builder->default_ext);
	builder->default_ext = g_strdup(ext);
}

void add_image(ManifestBuilder *builder, const gchar *slotclass)
{
	TestImage *image;

	image = g_new0(TestImage, 1);
	image->slotclass = g_strdup(slotclass);
	image->filename = g_strdup_printf("%s_image.%s", slotclass, builder->default_ext);

	g_hash_table_insert(builder->images, g_strdup(slotclass), image);
}

void add_file_to_image(ManifestBuilder *builder, const gchar *slotclass, const gchar *name)
{
	TestImage *test_image;

	test_image = g_hash_table_lookup(builder->images, slotclass);
	g_assert_nonnull(test_image);

	test_image->files = g_list_append(test_image->files, g_strdup(name));
}

void set_custom_handler(ManifestBuilder *builder)
{
	builder->custom_handler = TRUE;
}

void add_slot_hook(ManifestBuilder *builder, const gchar *slotclass, const gchar *name)
{
	TestImage *test_image;

	test_image = g_hash_table_lookup(builder->images, slotclass);
	g_assert_nonnull(test_image);

	builder->have_hooks = TRUE;
	test_image->hooks = g_list_append(test_image->hooks, g_strdup(name));
}

void add_install_hook(ManifestBuilder *builder, const gchar *name)
{
	builder->have_hooks = TRUE;
	builder->hooks = g_list_append(builder->hooks, g_strdup(name));
}

TestManifest* manifest_builder_end(ManifestBuilder *builder, gboolean inmemory)
{
	GString *manifest = g_string_new("");
	GHashTableIter iter;
	TestImage *iterimage;
	TestManifest *test_manifest;
	gchar *contentdir;

	/* write manifest */
	g_string_append(manifest, "[update]\n");
	g_string_append_printf(manifest, "compatible=%s\n", builder->compatible);
	g_string_append_c(manifest, '\n');

	if (builder->custom_handler) {
		g_string_append(manifest, "[handler]\n");
		g_string_append_printf(manifest, "filename=%s\n", "custom_handler.sh");
		g_string_append_c(manifest, '\n');
	}

	if (builder->have_hooks) {
		g_string_append(manifest, "[hooks]\n");
		g_string_append_printf(manifest, "filename=%s\n", "hook.sh");
		g_string_append_printf(manifest, "hooks=");
		for (GList *l = builder->hooks; l != NULL; l = l->next) {
			gchar *hookname = l->data;

			g_string_append_printf(manifest, "%s;", hookname);
		}
		g_string_append_c(manifest, '\n');
	}

	g_hash_table_iter_init(&iter, builder->images);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &iterimage)) {
		g_string_append_printf(manifest, "[image.%s]\n", iterimage->slotclass);
		g_string_append_printf(manifest, "filename=%s\n", iterimage->filename);

		/* add hooks to slotclasses */
		if (g_list_length(iterimage->hooks) > 0)
			g_string_append(manifest, "hooks=");
		for (GList *l = iterimage->hooks; l != NULL; l = l->next) {
			gchar *hookname = l->data;

			g_string_append_printf(manifest, "%s;", hookname);
		}

		g_string_append_c(manifest, '\n');
	}

	/* prepare content directory */
	g_assert(test_mkdir_relative(builder->tmpdir, "content", 0777) == 0);
	contentdir = g_build_filename(builder->tmpdir, "content", NULL);
	builder->contentdir = contentdir;

	test_manifest = g_new0(TestManifest, 1);

	if (inmemory) {
		test_manifest->data = g_string_free_to_bytes(manifest);
	} else {
		write_tmp_file(contentdir, "manifest.raucm", g_string_free(manifest, FALSE), NULL);
		test_manifest->pathname = g_build_filename(contentdir, "manifest.raucm", NULL);
	}

	return test_manifest;
}

BundleContent* bundle_content_from_manifest_builder(ManifestBuilder *builder)
{
	GHashTableIter iter;
	TestImage *iterimage;

	manifest_builder_end(builder, FALSE);

	/* prepare full custom handler */
	if (builder->custom_handler)
		g_assert_true(test_copy_file("test/install-content/custom_handler.sh", NULL,
				builder->contentdir, "custom_handler.sh"));

	/* prepare hooks */
	if (builder->have_hooks)
		g_assert_true(test_copy_file("test/install-content/hook.sh", NULL,
				builder->contentdir, "hook.sh"));

	g_hash_table_iter_init(&iter, builder->images);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &iterimage)) {
		g_assert(test_prepare_dummy_file(builder->contentdir, iterimage->filename,
				SLOT_SIZE, "/dev/zero") == 0);
		if (g_strcmp0(builder->default_ext, "ext4") == 0)
			g_assert_true(test_make_filesystem(contentdir, iterimage->filename));

		/* Write test files to images */
		for (GList *l = iterimage->files; l != NULL; l = l->next) {
			gchar *filename = l->data;
			gchar *mountdir;

			mountdir = g_build_filename(builder->tmpdir, "mnt", NULL);
			g_assert(test_mkdir_relative(builder->tmpdir, "mnt", 0777) == 0);
			g_assert_true(test_mount(g_build_filename(contentdir, iterimage->filename, NULL), mountdir));
			g_assert_true(g_file_set_contents(g_build_filename(mountdir, filename, NULL), "0xdeadbeaf", -1, NULL));
			g_assert_true(r_umount(mountdir, NULL));
			g_assert(test_rmdir(builder->tmpdir, "mnt") == 0);
		}
	}

	return builder;
}

TestBundle* test_bundle_from_bundle_content(BundleContent *builder)
{
	gchar *contentdir;
	gchar *bundlepath;
	gchar *certpath;
	gchar *keypath;
	TestBundle *return_bundle;

	g_assert(test_mkdir_relative(builder->tmpdir, "openssl-ca", 0777) == 0);

	/* copy cert */
	certpath = g_build_filename(builder->tmpdir, "openssl-ca/release-1.cert.pem", NULL);
	g_assert_nonnull(certpath);
	g_assert_true(test_copy_file("test/openssl-ca/rel/release-1.cert.pem", NULL, certpath, NULL));
	r_context_conf()->certpath = g_strdup(certpath);

	/* copy key */
	keypath = g_build_filename(builder->tmpdir, "openssl-ca/release-1.pem", NULL);
	g_assert_nonnull(keypath);
	g_assert_true(test_copy_file("test/openssl-ca/rel/private/release-1.pem", NULL, keypath, NULL));
	r_context_conf()->keypath = g_strdup(keypath);

	bundlepath = g_build_filename(builder->tmpdir, "bundle.raucb", NULL);

	contentdir = g_build_filename(builder->tmpdir, "content", NULL);

	/* Update checksums in manifest */
	g_assert_true(update_manifest(contentdir, FALSE, NULL));

	/* Create bundle */
	g_assert_true(create_bundle(bundlepath, contentdir, NULL));

	/* cleanup content/ dir */
	rm_tree(contentdir, NULL);

	return_bundle = g_new0(TestBundle, 1);
	return_bundle->tmpdir = builder->tmpdir;
	return_bundle->bundlepath = bundlepath;

	return return_bundle;
}

TestBundle* test_bundle_from_manifest_builder(ManifestBuilder *builder)
{
	BundleContent *bundle_content;
	TestBundle *test_bundle;

	bundle_content = bundle_content_from_manifest_builder(builder);
	test_bundle = test_bundle_from_bundle_content(bundle_content);

	return test_bundle;
}

void cleanup_test_manifest(TestManifest *test_manifest)
{
	if (!test_manifest)
		return;
}

void cleanup_test_bundle(TestBundle *bundle)
{
	if (!bundle)
		return;

	rm_tree(bundle->tmpdir, NULL);
}

ManifestBuilder* manifest_builder_default(void)
{
	ManifestBuilder *bundle_builder;

	bundle_builder =  manifest_builder_new();

	add_image(bundle_builder, "rootfs");
	add_image(bundle_builder, "appfs");

	add_file_to_image(bundle_builder, "rootfs", "verify.txt");

	return bundle_builder;
}

RaucSystemBuilder* test_config_builder_default(void)
{
	RaucSystemBuilder *builder;

	builder = test_config_builder_new();

	add_boot_slot(builder, "rootfs.0", "system0");
	add_boot_slot(builder, "rootfs.1", "system1");
	add_child_slot(builder, "appfs.0", "rootfs.0");
	add_child_slot(builder, "appfs.1", "rootfs.1");

	return builder;
}
