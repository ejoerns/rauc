#include <glib.h>
#include <gio/gio.h>
#if ENABLE_JSON
#include <json-glib/json-glib.h>
#endif

#include "context.h"
#include "event-log.h"

static const gchar *supported_event_types[] = {
	"all",
	R_EVENT_LOG_BOOT,
	R_EVENT_LOG_INSTALL,
	R_EVENT_LOG_SERVICE,
	R_EVENT_LOG_WRITE_SLOT,
	R_EVENT_LOG_BOOT_SELECTION,
};

gboolean r_event_log_is_supported_type(const gchar *type)
{
	return g_strv_contains(supported_event_types, type);
}

void r_event_log_message(const gchar *type, const gchar *message, ...)
{
	va_list list;
	g_autofree gchar *formatted = NULL;

	g_return_if_fail(message);

	va_start(list, message);
	formatted = g_strdup_vprintf(message, list);
	va_end(list);

	g_log_structured(R_EVENT_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE,
			"RAUC_EVENT_TYPE", type,
			"MESSAGE", "%s", formatted);
}

void r_event_log_free_logger(REventLogger *config)
{
	if (!config)
		return;

	//g_object_unref(config->logstream);

	return;
}

#define append_string_conditionally(string, key, value) \
	if (value) \
		g_string_append_printf(string, "\n                      "key ": %s", value);

static gchar *event_log_format_fields_json(GLogLevelFlags log_level,
		const GLogField *fields, gsize n_fields, gboolean pretty)
{
	g_autoptr(GDateTime) now = NULL;
	const gchar *now_formatted = NULL;
	GString *gstring;

	g_autoptr(JsonGenerator) gen = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();

	gstring = g_string_new(NULL);
	now = g_date_time_new_now_utc();
	now_formatted = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
	g_string_append_printf(gstring, "%s: ", now_formatted);

	json_builder_begin_object(builder);
	for (gsize j = 0; j < n_fields; j++) {
		const GLogField *ifield = &fields[j];

		json_builder_set_member_name(builder, ifield->key);
		json_builder_add_string_value(builder, ifield->value);
	}
	json_builder_end_object(builder);

	{
		gen = json_generator_new();
		root = json_builder_get_root(builder);
		json_generator_set_root(gen, root);
		json_generator_set_pretty(gen, pretty);
		return json_generator_to_data(gen, NULL);
	}
}

static gchar *event_log_format_fields_readable(GLogLevelFlags log_level,
		const GLogField *fields, gsize n_fields, gboolean verbose)
{
	g_autoptr(GDateTime) now = NULL;
	const gchar *now_formatted = NULL;
	GString *gstring;
	const gchar *message = NULL;
	const gchar *transaction_id = NULL;
	const gchar *bundle_hash = NULL;
	const gchar *boot_id = NULL;

	gstring = g_string_new(NULL);
	now = g_date_time_new_now_utc();
	now_formatted = g_date_time_format(now, "%Y-%m-%dT%H:%M:%SZ");
	g_string_append_printf(gstring, "%s: ", now_formatted);

	for (gsize j = 0; j < n_fields; j++) {
		const GLogField *ifield = &fields[j];

		if (g_strcmp0(ifield->key, "MESSAGE") == 0) {
			message = ifield->value;
		} else if (g_strcmp0(ifield->key, "TRANSACTION_ID") == 0) {
			transaction_id = ifield->value;
		} else if (g_strcmp0(ifield->key, "BUNDLE_HASH") == 0) {
			bundle_hash = ifield->value;
		} else if (g_strcmp0(ifield->key, "BOOT_ID") == 0) {
			boot_id = ifield->value;
		}
	}

	g_string_append_printf(gstring, "%s", message);
	if (verbose) {
		append_string_conditionally(gstring, "transaction ID", transaction_id);
		append_string_conditionally(gstring, "bundle hash", bundle_hash);
		append_string_conditionally(gstring, "boot ID", boot_id);
	}

	return g_string_free(gstring, FALSE);
}

static void event_log_writer_file(REventLogger* logger, const GLogField *fields, gsize n_fields)
{
	g_autoptr(GFile) logfile = NULL;
	GError *ierror = NULL;
	g_autofree gchar *formatted = NULL;
	g_autofree gchar *output = NULL;
	GFileOutputStream *logstream = NULL;

	g_assert_nonnull(logger->filename);

	logfile = g_file_new_for_path(logger->filename);
	logstream = g_file_append_to(logfile, G_FILE_CREATE_NONE, NULL, &ierror);
	if (!logstream) {
		g_warning("%s", ierror->message);
		return;
	}

	switch (logger->format) {
		case R_EVENT_LOGFMT_READABLE:
			formatted = event_log_format_fields_readable(G_LOG_LEVEL_MESSAGE, fields, n_fields, TRUE);
			break;
		case R_EVENT_LOGFMT_READABLE_SHORT:
			formatted = event_log_format_fields_readable(G_LOG_LEVEL_MESSAGE, fields, n_fields, FALSE);
			break;
		case R_EVENT_LOGFMT_JSON:
			formatted = event_log_format_fields_json(G_LOG_LEVEL_MESSAGE, fields, n_fields, FALSE);
			break;
		case R_EVENT_LOGFMT_JSON_PRETTY:
			formatted = event_log_format_fields_json(G_LOG_LEVEL_MESSAGE, fields, n_fields, TRUE);
			break;
		default:
			g_error("Unknow log format");
	}
	output = g_strdup_printf("%s\n", formatted);

	g_output_stream_write_all(G_OUTPUT_STREAM(logstream), output, strlen(output), NULL, NULL, NULL);
}

GLogWriterOutput r_event_log_writer(GLogLevelFlags log_level, const GLogField *fields, gsize n_fields, gpointer user_data)
{
	const gchar *log_domain;
	const gchar *event_type = NULL;

	/* Always log to default location, too */
	g_log_writer_default(log_level, fields, n_fields, user_data);

	/* get log domain */
	for (gsize i = 0; i < n_fields; i++) {
		if (g_strcmp0(fields[i].key, "GLIB_DOMAIN") == 0) {
			log_domain = fields[i].value;
			break;
		}
	}

	/* We are interested in "rauc-event" domains only */
	if (g_strcmp0(log_domain, R_EVENT_LOG_DOMAIN) != 0) {
		return G_LOG_WRITER_HANDLED;
	}

	/* get event type */
	for (gsize i = 0; i < n_fields; i++) {
		if (g_strcmp0(fields[i].key, "RAUC_EVENT_TYPE") == 0) {
			event_type = fields[i].value;
			break;
		}
	}

	/* iterate over registered event loggers */
	for (GList *l = r_context()->config->logger; l != NULL; l = l->next) {
		REventLogger* logger = l->data;

		/* Filter out by event type */
		if ((g_strcmp0(logger->events[0], "all") != 0) &&
		    !g_strv_contains((const gchar * const*)logger->events, event_type)) {
			continue;
		}

		logger->writer(logger, fields, n_fields);
	}

	return G_LOG_WRITER_HANDLED;
}

void r_event_log_setup_logger(REventLogger *logger)
{
	g_return_if_fail(logger);

	if (logger->configured) {
		g_message("Logger %s already configured", logger->name);
		return;
	}

	g_info("Setting up logger %s for %s ..", logger->name, logger->filename);

	logger->writer = &event_log_writer_file;

	logger->configured = TRUE;

	return;
}
