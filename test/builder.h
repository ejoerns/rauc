#pragma once

#include <glib.h>

#define SLOT_SIZE (10*1024*1024)

typedef struct {
	gchar *tmpdir;
	gchar *compatible;
	gboolean status_global;
	gchar *variant_name;
	gchar *keyring;
	GHashTable *slots;
	gchar *configpath; // only set for TestConfig
} RaucSystemBuilder;

typedef RaucSystemBuilder TestConfig;

typedef struct {
	gchar *tmpdir;
} TestSystem;

/**
 * Allocates and instantiates a new RaucSystemBuilder.
 *
 * To create a new TestConfig, call test_config_builder_end().
 */
RaucSystemBuilder* test_config_builder_new(void);

/**
 * Set RAUC system compatible in builder.
 *
 * @param builder a RaucSystemBuilder
 * @param compatible compatible string to set
 */
void test_config_set_compatible(RaucSystemBuilder *builder, const gchar *compatible);

/**
 * Set RAUC variant-name in builder.
 *
 * @param builder a RaucSystemBuilder
 * @param variant variant-name string to set
 */
void test_config_set_variant_name(RaucSystemBuilder *builder, const gchar *variant);

/**
 * Set to use global slot status file
 *
 * @param builder a RaucSystemBuilder
 */
void set_global_status(RaucSystemBuilder *builder);

/**
 * Add a handler.
 *
 * @param builder a RaucSystemBuilder
 * @param name name of handler to add
 * @param content content of handler to add
 */
void add_handler(RaucSystemBuilder *builder, const gchar *name, const gchar *content);

/**
 * Add a simple slot to system config.
 *
 * @param builder a RaucSystemBuilder
 * @param slotname name of slot to add
 */
void add_slot(RaucSystemBuilder *builder, const gchar *slotname);

/**
 * Add a simple slot to system config.
 *
 * @param builder a RaucSystemBuilder
 * @param slotname name of slot to add
 * @param bootname bootname of slot to add
 */
void add_boot_slot(RaucSystemBuilder *builder, const gchar *slotname, const gchar *bootname);

/**
 * Add a simple slot to system config.
 *
 * @param builder a RaucSystemBuilder
 * @param slotname name of slot to add
 * @param parent name of slot to add this slot as child for
 */
void add_child_slot(RaucSystemBuilder *builder, const gchar *slotname, const gchar *parent);

/**
 * Set readonly property for a slot.
 *
 * @param builder a RaucSystemBuilder
 * @param slotname name of already-added slot to set readonly attribute for
 */
void set_slot_readonly(RaucSystemBuilder *builder, const gchar *slotname);

/**
 * Creates a system configuration files from a RaucSystemBuilder.
 *
 * Clean up using cleanup_test_config()
 *
 * @param builder a RaucSystemBuilder
 * @return TestConfig instance containing the path to the generated system.conf
 */
TestConfig* test_config_builder_end(RaucSystemBuilder *builder);

/**
 * Clean up a TestConfig.
 *
 * Will remove generated files.
 *
 * @param test_config TestConfig to clean up.
 */
void cleanup_test_config(TestConfig *test_config);

/**
 * Create a full test system from a given RaucSystemBuilder.
 *
 * @param builder a RaucSystemBuilder
 * @return TestSystem instance containing the path to the generated system
 */
TestSystem* test_system_from_test_config(TestConfig *config, gboolean root);

/**
 * Clean up a TestSystem.
 *
 * Will remove generated files
 *
 * @param system TestSystem to clean up.
 */
void cleanup_test_system(TestSystem *system);

///

typedef struct {
	gchar *tmpdir;
	gchar *compatible;
	gchar *default_ext;
	GHashTable *images;
	gboolean custom_handler;
	gboolean have_hooks;
	GList* hooks;
	gchar *contentdir;
} ManifestBuilder;

typedef struct {
	gchar *tmpdir;
	gchar *bundlepath;
} TestBundle;

typedef ManifestBuilder BundleContent;

typedef struct {
	GBytes *data;
	gchar *pathname;
} TestManifest;

/**
 * Allocates and instantiates a new ManifestBuilder.
 *
 * To create a new Manifest, call manifest_builder_end().
 */
ManifestBuilder* manifest_builder_new(void);

/**
 * Set the default file name extension to use for slots.
 *
 * The default is ext4.
 *
 * @param builder a ManifestBuilder
 * @param ext extensions to use, e.g. "ext4"
 */
void set_default_ext(ManifestBuilder *builder, gchar *ext);

/**
 * Set manifest builder to use full custom handler.
 *
 * The custom handler ist name 'custom_handler.sh'
 *
 * @param builder a ManifestBuilder
 */
void set_custom_handler(ManifestBuilder *builder);

/**
 * Adds an image to the manifest.
 *
 * @param builder a ManifestBuilder
 * @param slotclass slot class to add an image for
 */
void add_image(ManifestBuilder *builder, const gchar *slotclass);

/**
 * Adds a file to the image.
 *
 * One can specify the name under which the file should be created in the
 * images file system.
 * The file will get a default content of string "0xdeadbeaf"
 *
 * @param builder a ManifestBuilder
 * @param slotclass slotclass of image ot add file for
 * @param name file name to create (must not contain subdirectories)
 */
void add_file_to_image(ManifestBuilder *builder, const gchar *slotclass, const gchar *name);

/**
 * Adds a slot hook for an images slot.
 *
 * @param builder a ManifestBuilder
 * @param slotclass slotclass of image ot add file for
 * @param name name of slot hook to add, e.g. 'post-install'
 */
void add_slot_hook(ManifestBuilder *builder, const gchar *slotclass, const gchar *name);

/**
 * Adds an install hook for the manifest.
 *
 * @param builder a ManifestBuilder
 * @param name name of install hook to add
 */
void add_install_hook(ManifestBuilder *builder, const gchar *name);

/**
 * Creates a manifest from a ManifestBuilder.
 *
 * Clean up using cleanup_test_manifest()
 *
 * @param builder a ManifestBuilder
 * @return TestManifest instance containing the path to the generated manifest.raucm
 */
TestManifest* manifest_builder_end(ManifestBuilder *builder, gboolean inmemory);

/**
 */
BundleContent* bundle_content_from_manifest_builder(ManifestBuilder *builder);

/**
 * Creates a test bundle given a BundleContent.
 *
 * @param bundle_content a BundleContent
 */
TestBundle* test_bundle_from_bundle_content(BundleContent *bundle_content);

/**
 * Creates a TestBundle from a manifest builder.
 *
 * @param builder a ManifestBuilder
 */
TestBundle* test_bundle_from_manifest_builder(ManifestBuilder *builder);

/**
 * Clean up a TestBundle.
 *
 * Will remove generated files.
 *
 * @param test_bundle TestBundle to clean up.
 */
void cleanup_test_bundle(TestBundle *test_bundle);

/**
 * Clean up a TestManifest.
 *
 * Will remove generated files.
 *
 * @param test_manifest TestManifest to clean up.
 */
void cleanup_test_manifest(TestManifest *test_manifest);

/* convenience */

ManifestBuilder* manifest_builder_default(void);

RaucSystemBuilder* test_config_builder_default(void);
