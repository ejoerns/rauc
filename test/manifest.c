#include <stdio.h>
#include <locale.h>
#include <glib.h>

#include <config_file.h>
#include <context.h>
#include <manifest.h>

#include "utils.h"

static void manifest_check_common(RaucManifest *rm) {
	g_assert_nonnull(rm);
	g_assert_cmpstr(rm->update_compatible, ==, "FooCorp Super BarBazzer");
	g_assert_cmpstr(rm->update_version, ==, "2015.04-1");
	g_assert_cmpstr(rm->keyring, ==, "release.tar");
	g_assert_cmpstr(rm->handler_name, ==, "custom_handler.sh");
	g_assert_cmpstr(rm->handler_args, ==, "--dummy1 --dummy2");
	g_assert_cmpstr(rm->hook_name, ==, "hook.sh");
	g_assert_nonnull(rm->images);

	g_assert_cmpuint(g_list_length(rm->images), ==, 2);
	g_assert_cmpuint(g_list_length(rm->files), ==, 2);

	for (GList *l = rm->images; l != NULL; l = l->next) {
		RaucImage *img = l->data;
		g_assert_nonnull(img);
		g_assert_nonnull(img->slotclass);
		g_assert_nonnull(img->checksum.digest);
		g_assert_nonnull(img->filename);
	}

	for (GList *l = rm->files; l != NULL; l = l->next) {
		RaucFile *file = l->data;
		g_assert_nonnull(file);
		g_assert_nonnull(file->slotclass);
		g_assert_nonnull(file->destname);
		g_assert_nonnull(file->checksum.digest);
		g_assert_nonnull(file->filename);
	}
}

/* Test manifest/load:
 *
 * Tests loading manifest from file: *
 * Test cases:
 * - load a valid manifest file
 * - load a non-exisiting manifest file
 * - load a broken manifest file
 */
static void test_load_manifest(void)
{
	RaucManifest *rm = NULL;
	GError *error = NULL;

	// Load valid manifest file
	g_assert_true(load_manifest_file("test/manifest.raucm", &rm, &error));
	g_assert_null(error);
	manifest_check_common(rm);

	g_clear_pointer(&rm, free_manifest);
	g_assert_null(rm);

	// Load non-existing manifest file
	g_assert_false(load_manifest_file("test/nonexisting.raucm", &rm, &error));
	g_assert_nonnull(error);

	g_clear_pointer(&rm, free_manifest);
	g_clear_error(&error);
	g_assert_null(rm);

	// Load broken manifest file
	g_assert_false(load_manifest_file("test/broken-manifest.raucm", &rm, &error));
	g_assert_nonnull(error);

	g_clear_pointer(&rm, free_manifest);
	g_clear_error(&error);
	g_assert_null(rm);

}

/* Test manifest/save_load:
 *
 * Tests saving manifest structure to file and load again.
 *
 * Test cases:
 * - save a valid manifest to file and load
 */
static void test_save_load_manifest(void)
{
	RaucManifest *rm = g_new0(RaucManifest, 1);
	RaucImage *new_image;
	RaucFile *new_file;

	rm->update_compatible = g_strdup("BarCorp FooBazzer");
	rm->update_version = g_strdup("2011.03-1");
	rm->keyring = g_strdup("mykeyring.tar");
	rm->handler_name = g_strdup("myhandler.sh");
	rm->handler_args = g_strdup("--foo");
	rm->hook_name = g_strdup("hook.sh");

	new_image = g_new0(RaucImage, 1);

	new_image->slotclass = g_strdup("rootfs");
	new_image->checksum.type = G_CHECKSUM_SHA256;
	new_image->checksum.digest = g_strdup("c8af04e62bad4ab75dafd22119026e5e3943f385bdcbe7731a4938102453754c");
	new_image->filename = g_strdup("myrootimg.ext4");
	new_image->hooks.pre_install = TRUE;
	new_image->hooks.post_install = TRUE;
	rm->images = g_list_append(rm->images, new_image);

	new_image = g_new0(RaucImage, 1);

	new_image->slotclass = g_strdup("appfs");
	new_image->checksum.type = G_CHECKSUM_SHA256;
	new_image->checksum.digest = g_strdup("4e7e45db749b073eda450d30c978c7e2f6035b057d3e33ac4c61d69ce5155313");
	new_image->filename = g_strdup("myappimg.ext4");
	rm->images = g_list_append(rm->images, new_image);

	new_file = g_new0(RaucFile, 1);

	new_file->slotclass = g_strdup("rootfs");
	new_file->destname = g_strdup("vmlinuz");
	new_file->checksum.type = G_CHECKSUM_SHA256;
	new_file->checksum.digest = g_strdup("5ce231b9683db16623783b8bcff120c11969e8d29755f25bc87a6fae92e06741");
	new_file->filename = g_strdup("mykernel.img");
	rm->files = g_list_append(rm->files, new_file);

	g_assert_cmpuint(g_list_length(rm->images), ==, 2);
	g_assert_cmpuint(g_list_length(rm->files), ==, 1);

	g_assert_true(save_manifest_file("test/savedmanifest.raucm", rm, NULL));

	g_clear_pointer(&rm, free_manifest);

	g_assert_true(load_manifest_file("test/savedmanifest.raucm", &rm, NULL));

	g_assert_nonnull(rm);
	g_assert_cmpstr(rm->update_compatible, ==, "BarCorp FooBazzer");
	g_assert_cmpstr(rm->update_version , ==, "2011.03-1");
	g_assert_cmpstr(rm->keyring, ==, "mykeyring.tar");
	g_assert_cmpstr(rm->handler_name, ==, "myhandler.sh");
	g_assert_cmpstr(rm->handler_args, ==, "--foo --dummy1 --dummy2");
	g_assert_cmpstr(rm->hook_name, ==, "hook.sh");

	g_assert_cmpuint(g_list_length(rm->images), ==, 2);
	g_assert_cmpuint(g_list_length(rm->files), ==, 1);

	for (GList *l = rm->images; l != NULL; l = l->next) {
		RaucImage *image = (RaucImage*) l->data;
		g_assert_nonnull(image);
		g_assert_nonnull(image->slotclass);
		g_assert_nonnull(image->checksum.digest);
		g_assert_nonnull(image->filename);
	}

	for (GList *l = rm->files; l != NULL; l = l->next) {
		RaucFile *file = l->data;
		g_assert_nonnull(file);
		g_assert_nonnull(file->slotclass);
		g_assert_nonnull(file->destname);
		g_assert_nonnull(file->checksum.digest);
		g_assert_nonnull(file->filename);
	}

	g_assert_true(((RaucImage*)g_list_nth_data(rm->images, 0))->hooks.pre_install);
	g_assert_true(((RaucImage*)g_list_nth_data(rm->images, 0))->hooks.post_install);

	free_manifest(rm);
}


/* Test manifest/load_mem:
 *
 * Tests loading manifest from memory.
 *
 * Test cases:
 * - load a valid manifest from memory
 */
static void test_load_manifest_mem(void)
{
	GBytes *data = NULL;
	RaucManifest *rm = NULL;

	data = read_file("test/manifest.raucm", NULL);
	g_assert_true(load_manifest_mem(data, &rm, NULL));
	manifest_check_common(rm);

	free_manifest(rm);
}

/* Test manifest/invalid_data:
 *
 * Tests parsing invalid data: *
 * Test cases:
 * - file does not start with a group
 * - compatible is mising
 * - compatible has no value
 * - invalid key
 * - invalid group
 */
static void test_invalid_data(void)
{
	GBytes *data = NULL;
	GError *error = NULL;
	RaucManifest *rm = NULL;

	// file does not start with a group
#define MANIFEST1 "\
compatible=SuperBazzer\n\
"

	// compatible is mising
#define MANIFEST2 "\
[update]\n\
"

	// compatible has no value
#define MANIFEST3 "\
[update]\n\
compatible=\n\
"

	// invalid key
#define MANIFEST4 "\
[update]\n\
compatible=SuperBazzer\n\
evilkey=foo\n\
"

	// invalid group
#define MANIFEST5 "\
[update]\n\
compatible=SuperBazzer\n\
\n\
[evilgroup]\n\
"

	data = g_bytes_new_static(MANIFEST1, sizeof(MANIFEST1));
	g_assert_false(load_manifest_mem(data, &rm, &error));
	g_assert_nonnull(error);
	g_assert_cmpstr("Key file does not start with a group", ==, error->message);
	g_clear_error(&error);
	g_assert_null(rm);
	g_clear_pointer(&data, g_byte_array_free);

	data = g_bytes_new_static(MANIFEST2, sizeof(MANIFEST2));
	g_assert_false(load_manifest_mem(data, &rm, &error));
	g_assert_nonnull(error);
	g_assert_cmpstr("Key file does not have key 'compatible'", ==, error->message);
	g_clear_error(&error);
	g_assert_null(rm);
	g_clear_pointer(&data, g_byte_array_free);

	data = g_bytes_new_static(MANIFEST3, sizeof(MANIFEST3));
	g_assert_false(load_manifest_mem(data, &rm, &error));
	g_assert_nonnull(error);
	g_assert_cmpstr("Missing value for key 'compatible'", ==, error->message);
	g_clear_error(&error);
	g_assert_null(rm);
	g_clear_pointer(&data, g_byte_array_free);

	data = g_bytes_new_static(MANIFEST4, sizeof(MANIFEST4));
	g_assert_false(load_manifest_mem(data, &rm, &error));
	g_assert_nonnull(error);
	g_assert_cmpstr("Invalid key 'evilkey' in group '[update]'", ==, error->message);
	g_clear_error(&error);
	g_assert_null(rm);
	g_clear_pointer(&data, g_byte_array_free);

	data = g_bytes_new_static(MANIFEST5, sizeof(MANIFEST5));
	g_assert_false(load_manifest_mem(data, &rm, &error));
	g_assert_nonnull(error);
	g_assert_cmpstr("Invalid group '[evilgroup]'", ==, error->message);
	g_clear_error(&error);
	g_assert_null(rm);
	g_clear_pointer(&data, g_byte_array_free);
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "C");

	g_test_init(&argc, &argv, NULL);

	r_context_conf()->configpath = g_strdup("test/test.conf");
	r_context_conf()->handlerextra = g_strdup("--dummy1 --dummy2");
	r_context();

	g_test_add_func("/manifest/load", test_load_manifest);
	g_test_add_func("/manifest/save_load", test_save_load_manifest);
	g_test_add_func("/manifest/load_mem", test_load_manifest_mem);
	g_test_add_func("/manifest/invalid_data", test_invalid_data);

	return g_test_run ();
}
