#include <config.h>

#include <stdio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <config.h>
#include <bootchooser.h>
#include <bundle.h>
#include <config_file.h>
#include <context.h>
#include <install.h>
#include <service.h>
#include "rauc-installer-generated.h"

GMainLoop *r_loop = NULL;
int r_exit_status = 0;

static gboolean install_notify(gpointer data) {
	RaucInstallArgs *args = data;

	g_mutex_lock(&args->status_mutex);
	while (!g_queue_is_empty(&args->status_messages)) {
		gchar *msg = g_queue_pop_head(&args->status_messages);
		g_message("installing %s: %s", args->name, msg);
	}
	r_exit_status = args->status_result;
	g_mutex_unlock(&args->status_mutex);

	return G_SOURCE_REMOVE;
}

static gboolean install_cleanup(gpointer data)
{
	RaucInstallArgs *args = data;

	g_mutex_lock(&args->status_mutex);
	g_message("installing %s done: %d", args->name, args->status_result);
	r_exit_status = args->status_result;
	g_mutex_unlock(&args->status_mutex);

	install_args_free(args);

	g_main_loop_quit(r_loop);

	return G_SOURCE_REMOVE;
}

static void on_installer_changed(GDBusProxy *proxy, GVariant *changed,
				 const gchar* const *invalidated,
				 gpointer data) {
	RaucInstallArgs *args = data;
	gchar *msg;

	if (invalidated && invalidated[0]) {
		g_warning("rauc service disappeared\n");
		g_mutex_lock(&args->status_mutex);
		args->status_result = 2;
		g_mutex_unlock(&args->status_mutex);
		args->cleanup(args);
		return;
	}

	g_mutex_lock(&args->status_mutex);
	if (g_variant_lookup(changed, "Operation", "s", &msg)) {
		g_queue_push_tail(&args->status_messages, g_strdup(msg));
	}
	g_mutex_unlock(&args->status_mutex);

	if (!g_queue_is_empty(&args->status_messages)) {
		args->notify(args);
	}
}

static void on_installer_completed(GDBusProxy *proxy, gint result,
				   gpointer data) {
	RaucInstallArgs *args = data;

	g_mutex_lock(&args->status_mutex);
	args->status_result = result;
	g_mutex_unlock(&args->status_mutex);

	if (result >= 0) {
		args->cleanup(args);
	}
}

static gboolean install_start(int argc, char **argv)
{
	RInstaller *installer = NULL;
	RaucInstallArgs *args = install_args_new();
	GError *error = NULL;
	gchar *bundlelocation = NULL, *bundlescheme = NULL;

	g_debug("install started\n");

	if (argc < 3) {
		g_error("a bundle filename name must be provided");
		r_exit_status = 1;
		goto out;
	}

	bundlescheme = g_uri_parse_scheme(argv[2]);
	if (bundlescheme == NULL && !g_path_is_absolute(argv[2])) {
		bundlelocation = g_build_filename(g_get_current_dir(), argv[2], NULL);
	} else {
		bundlelocation = g_strdup(argv[2]);
	}
	g_clear_pointer(&bundlescheme, g_free);
	g_debug("input bundle: %s", bundlelocation);


	args->name = bundlelocation;
	args->notify = install_notify;
	args->cleanup = install_cleanup;

	r_loop = g_main_loop_new(NULL, FALSE);
	if (ENABLE_SERVICE) {
		installer = r_installer_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
			"de.pengutronix.rauc", "/", NULL, NULL);
		if (g_signal_connect(installer, "g-properties-changed",
				     G_CALLBACK(on_installer_changed), args) <= 0) {
			g_error("failed to connect properties-changed signal");
			goto out;
		}
		if (g_signal_connect(installer, "completed",
				     G_CALLBACK(on_installer_completed), args) <= 0) {
			g_error("failed to connect completed signal");
			goto out;
		}
		g_print("trying to contact rauc service\n");
		if (!r_installer_call_install_sync(installer, bundlelocation, NULL,
						   &error)) {
			g_warning("failed %s", error->message);
			goto out;
		}
	} else {
		install_run(args);
	}

	g_main_loop_run(r_loop);
out:
	g_clear_pointer(&r_loop, g_main_loop_unref);
	g_clear_pointer(&installer, g_object_unref);
	return TRUE;
}

static gboolean bundle_start(int argc, char **argv)
{
	GError *ierror = NULL;
	g_debug("bundle start");

	if (r_context()->certpath == NULL ||
	    r_context()->keypath == NULL) {
		g_warning("cert and key files must be provided");
		r_exit_status = 1;
		goto out;
	}

	if (argc < 3) {
		g_warning("an input directory name must be provided");
		r_exit_status = 1;
		goto out;
	}

	if (argc != 4) {
		g_warning("an output bundle name must be provided");
		r_exit_status = 1;
		goto out;
	}

	g_print("input directory: %s\n", argv[2]);
	g_print("output bundle: %s\n", argv[3]);

	if (!update_manifest(argv[2], FALSE, &ierror)) {
		g_warning("failed to update manifest: %s", ierror->message);
		r_exit_status = 1;
		goto out;
	}

	if (!create_bundle(argv[3], argv[2], &ierror)) {
		g_warning("failed to create bundle: %s", ierror->message);
		r_exit_status = 1;
		goto out;
	}

out:
	return TRUE;
}

static gboolean checksum_start(int argc, char **argv)
{
	GError *error = NULL;
	gboolean sign = FALSE;

	g_debug("checksum start");

	if (r_context()->certpath != NULL &&
	    r_context()->keypath != NULL) {
		sign = TRUE;
	} else if (r_context()->certpath != NULL ||
	    r_context()->keypath != NULL) {
		g_warning("Either both or none of cert and key files must be provided");
		r_exit_status = 1;
		goto out;
	}

	if (argc != 3) {
		g_warning("A directory name must be provided");
		r_exit_status = 1;
		goto out;
	}

	g_message("updating checksums for: %s", argv[2]);

	if (!update_manifest(argv[2], sign, &error)) {
		g_warning("Failed to update manifest: %s", error->message);
		g_clear_error(&error);
		r_exit_status = 1;
	}

out:
	return TRUE;
}

static gboolean info_start(int argc, char **argv)
{
	gsize size;
	gchar* tmpdir;
	gchar* bundledir;
	gchar* manifestpath;
	RaucManifest *manifest = NULL;
	GError *error = NULL;
	gboolean res = FALSE;
	gint cnt = 0;

	if (argc != 3) {
		g_warning("a file name must be provided");
		return FALSE;
	}

	g_message("checking manifest for: %s", argv[2]);

	tmpdir = g_dir_make_tmp("bundle-XXXXXX", NULL);
	bundledir = g_build_filename(tmpdir, "bundle-content", NULL);
	manifestpath = g_build_filename(bundledir, "manifest.raucm", NULL);

	res = check_bundle(argv[2], &size, &error);
	if (res) {
		g_message("signature correct (squashfs size: %"G_GSIZE_FORMAT")", size);
	} else {
		g_message("Signature invalid for current system: %s", error->message);
		g_clear_error(&error);
	}

	res = extract_bundle(argv[2], bundledir, 0, &error);
	if (!res) {
		g_warning("%s", error->message);
		g_clear_error(&error);
		goto out;
	}


	res = load_manifest_file(manifestpath, &manifest, &error);
	if (!res) {
		g_warning("%s", error->message);
		g_clear_error(&error);
		goto out;
	}

	g_message("Compatible String:\t'%s'", manifest->update_compatible);

	cnt = g_list_length(manifest->images);
	g_message("%d Image%s%s", cnt, cnt == 1 ? "" : "s", cnt > 0 ? ":" : "");
	cnt = 0;
	for (GList *l = manifest->images; l != NULL; l = l->next) {
		RaucImage *img = l->data;
		g_message("(%d)\t%s", ++cnt, img->filename);
		g_message("\tSlotclass: %s", img->slotclass);
		g_message("\tChecksum:  %s", img->checksum.digest);
	}

	cnt = g_list_length(manifest->files);
	g_message("%d File%s%s", cnt, cnt == 1 ? "" : "s", cnt > 0 ? ":" : "");
	cnt = 0;
	for (GList *l = manifest->files; l != NULL; l = l->next) {
		RaucFile *file = l->data;
		g_message("(%d)\t%s", ++cnt, file->filename);
		g_message("\tSlotclass: %s", file->slotclass);
		g_message("\tDest: 	%s", file->destname);
		g_message("\tChecksum:  %s", file->checksum.digest);
	}

out:
	r_exit_status = res ? 0 : 1;
	g_rmdir(tmpdir);

	g_clear_pointer(&tmpdir, g_free);
	g_clear_pointer(&bundledir, g_free);
	g_clear_pointer(&manifestpath, g_free);
	return TRUE;
}

static gboolean status_start(int argc, char **argv)
{
	GHashTableIter iter;
	gpointer key, value;
	gboolean res = FALSE;
	RaucSlot *booted = NULL;

	g_debug("status start\n");

	g_print("booted from: %s\n", get_bootname());

	res = determine_slot_states();
	if (!res) {
		g_warning("Failed to determine slot states");
		r_exit_status = 1;
		goto out;
	}

	g_print("slot states:\n");
	g_hash_table_iter_init(&iter, r_context()->config->slots);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		gchar *name = key;
		RaucSlot *slot = value;
		const gchar *state = NULL;
		switch (slot->state) {
		case ST_ACTIVE:
			state = "active";
			break;
		case ST_INACTIVE:
			state = "inactive";
			break;
		case ST_BOOTED:
			state = "booted";
			booted = slot;
			break;
		case ST_UNKNOWN:
		default:
			g_error("invalid slot status");
			r_exit_status = 1;
			break;
		}
		g_print("  %s: class=%s, device=%s, type=%s, bootname=%s\n",
			name, slot->sclass, slot->device, slot->type, slot->bootname);
		g_print("      state=%s", state);
		if (slot->parent)
			g_print(", parent=%s", slot->parent->name);
		else
			g_print(", parent=(none)");
		if (slot->mountpoint)
			g_print(", mountpoint=%s", slot->mountpoint);
		else
			g_print(", mountpoint=(none)");
		g_print("\n");
	}

	if (argc < 3) {
		r_exit_status = 1;
		goto out;
	}

	if (!booted) {
		g_warning("Failed to determine booted slot");
		r_exit_status = 1;
		goto out;
	}

	if (g_strcmp0(argv[2], "mark-good") == 0) {
		g_print("marking slot %s as good\n", booted->name);
		r_exit_status = 0;
		r_boot_set_state(booted, TRUE);
	} else if (g_strcmp0(argv[2], "mark-bad") == 0) {
		g_print("marking slot %s as bad\n", booted->name);
		r_exit_status = 0;
		r_boot_set_state(booted, FALSE);
	} else {
		g_message("unknown subcommand %s", argv[2]);
		r_exit_status = 1;
	}

out:
	return TRUE;
}

#if ENABLE_SERVICE == 1
static gboolean service_start(int argc, char **argv)
{
	g_debug("service start");

	return r_service_run();
}
#endif

static gboolean unknown_start(int argc, char **argv)
{
	g_debug("unknown start");

	return TRUE;
}

typedef enum  {
	UNKNOWN = 0,
	INSTALL,
	BUNDLE,
	CHECKSUM,
	STATUS,
	INFO,
	SERVICE,
} RaucCommandType;

typedef struct {
	const RaucCommandType type;
	const gchar* name;
	const gchar* usage;
	gboolean (*cmd_handler) (int argc, char **argv);
	gboolean while_busy;
} RaucCommand;

static void cmdline_handler(int argc, char **argv)
{
	gboolean help = FALSE, version = FALSE;
	gchar *confpath = NULL, *certpath = NULL, *keypath = NULL, *mount = NULL,
	      *handlerextra = NULL;
	GOptionContext *context = NULL;
	GOptionEntry entries[] = {
		{"conf", 'c', 0, G_OPTION_ARG_FILENAME, &confpath, "config file", "FILENAME"},
		{"cert", '\0', 0, G_OPTION_ARG_FILENAME, &certpath, "cert file", "PEMFILE"},
		{"key", '\0', 0, G_OPTION_ARG_FILENAME, &keypath, "key file", "PEMFILE"},
		{"mount", '\0', 0, G_OPTION_ARG_FILENAME, &mount, "mount prefix", "PATH"},
		{"handler-args", '\0', 0, G_OPTION_ARG_STRING, &handlerextra, "extra handler arguments", "ARGS"},
		{"version", '\0', 0, G_OPTION_ARG_NONE, &version, "display version", NULL},
		{"help", 'h', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
		{0}
	};
	GError *error = NULL;

	RaucCommand rcommands[] = {
		{UNKNOWN, "help", "<COMMAND>", unknown_start, TRUE},
		{INSTALL, "install", "install <BUNDLE>", install_start, FALSE},
		{BUNDLE, "bundle", "bundle <FILE>", bundle_start, FALSE},
		{CHECKSUM, "checksum", "checksum <DIRECTORY>", checksum_start, FALSE},
		{INFO, "info", "info <FILE>", info_start, FALSE},
		{STATUS, "status", "status", status_start, TRUE},
#if ENABLE_SERVICE == 1
		{SERVICE, "service", "service", service_start, TRUE},
#endif
		{0}
	};
	RaucCommand *rcommand = &rcommands[0];

	/* show command-specific usage output */
	context = g_option_context_new(rcommand->usage);

	g_option_context_set_help_enabled(context, FALSE);
	g_option_context_set_ignore_unknown_options(context, TRUE);
	g_option_context_add_main_entries(context, entries, NULL);

	if (rcommand->type == UNKNOWN) {
		g_option_context_set_description(context, 
				"List of rauc commands:\n" \
				"  bundle\tCreate a bundle\n" \
				"  checksum\tUpdate a manifest with checksums (and optionally sign it)\n" \
				"  resign\tResign a bundle\n" \
				"  install\tInstall a bundle\n" \
				"  info\t\tShow file information\n" \
				"  status\tShow status");
	}

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_error("%s\n", error->message);
		g_error_free(error);
		r_exit_status = 1;
		goto done;
	}

	/* search for command (first option not starting with '-') */
	for (gint i = 1; i <= argc; i++) {
		RaucCommand *rc = rcommands;

		if (!argv[i] || g_str_has_prefix (argv[i], "-")) {
			continue;
		}

		/* test if known command */
		while (rc->name) {
			if (g_strcmp0(rc->name, argv[i]) == 0) {
				rcommand = rc;
				break;
			}
			rc++;
		}
		break;
	}

	if (version) {
		g_print(PACKAGE_STRING"\n");
		goto done;
	} else if (help || rcommand->type == UNKNOWN) {
		gchar *text;
		text = g_option_context_get_help(context, FALSE, NULL);
		g_print("%s", text);
		g_free(text);
		goto done;
	}

	/* configuration updates are handled here */
	if (!r_context_get_busy()) {
		r_context_conf();
		if (confpath)
			r_context_conf()->configpath = confpath;
		if (certpath)
			r_context_conf()->certpath = certpath;
		if (keypath)
			r_context_conf()->keypath = keypath;
		if (mount)
			r_context_conf()->mountprefix = mount;
		if (handlerextra)
			r_context_conf()->handlerextra = handlerextra;
	} else {
		if (confpath != NULL ||
		    certpath != NULL ||
		    keypath != NULL) {
			g_error("rauc busy, cannot reconfigure");
			r_exit_status = 1;
			goto done;
		}
	}

	if (r_context_get_busy() && !rcommand->while_busy) {
		g_error("rauc busy: cannot run %s", rcommand->name);
		r_exit_status = 1;
		goto done;
	}

	/* real commands are handled here */
	if (rcommand->cmd_handler) {
		rcommand->cmd_handler(argc, argv);
		goto done;
	}

done:
	g_clear_pointer(&context, g_option_context_free);;
}

int main(int argc, char **argv) {
	cmdline_handler(argc, argv);

	return r_exit_status;
}
