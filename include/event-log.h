#pragma once

#include <glib.h>
#include <gio/gio.h>

#define R_EVENT_LOG_DOMAIN "rauc-event"

#define R_EVENT_LOG_BOOT "boot"
#define R_EVENT_LOG_INSTALL "install"
#define R_EVENT_LOG_SERVICE "service"
#define R_EVENT_LOG_WRITE_SLOT "writeslot"
#define R_EVENT_LOG_BOOT_SELECTION "bootsel"

typedef struct _REventLogger REventLogger;

typedef enum {
	R_EVENT_LOGFMT_READABLE,
	R_EVENT_LOGFMT_READABLE_SHORT,
	R_EVENT_LOGFMT_JSON,
	R_EVENT_LOGFMT_JSON_PRETTY,
} REventLogFormat;

typedef struct _REventLogger {
	gchar *name;
	gchar *filename;
	gchar **events;
	REventLogFormat format;
	gboolean configured;
	GFileOutputStream *logstream;
	void (*writer)(REventLogger* logger, const GLogField *fields, gsize n_fields);
} REventLogger;

/**
 * Custom structured logging func.
 */
GLogWriterOutput r_event_log_writer(GLogLevelFlags log_level, const GLogField *fields, gsize n_fields, gpointer user_data);

/**
 */
void r_event_log_setup_logger(REventLogger *config);

/**
 * @param config
 */
void r_event_log_free_logger(REventLogger *config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(REventLogger, r_event_log_free_logger);

/**
 * @param type
 * @param message
 */
void r_event_log_message(const gchar *type, const gchar *message, ...)
__attribute__((__format__(__printf__, 2, 3)));
