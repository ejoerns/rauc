#pragma once

#include <glib.h>
#include <gio/gio.h>

#define R_NBD_ERROR r_nbd_error_quark()
GQuark r_nbd_error_quark(void);

typedef enum {
	R_NBD_ERROR_CONFIGURATION,
	R_NBD_ERROR_STARTUP,
	R_NBD_ERROR_UNAUTHORIZED,
	R_NBD_ERROR_NOT_FOUND,
} RNBDError;

typedef struct {
	gint sock;
	guint32 index;
	gboolean index_valid;
	gchar *dev;
	guint64 data_size;
} RaucNBDDevice;

typedef struct {
	gint sock; /* client side socket */
	GSubprocess *sproc;

	/* configuration */
	gchar *url;
	gchar *tls_cert; /* local file or PKCS#11 URI */
	gchar *tls_key; /* local file or PKCS#11 URI */
	gchar *tls_ca; /* local file */
	gboolean tls_no_verify;
	GStrv headers; /* array of strings such as 'Foo: bar' */

	/* discovered information */
	guint64 data_size; /* bundle size */
	gchar *effective_url; /* url after redirects */
	guint64 current_time; /* date header from server */
	guint64 modified_time; /* last-modified header from server */
} RaucNBDServer;

typedef struct {
} RaucNBDConfig;

RaucNBDDevice *new_nbd_device(void);
void free_nbd_device(RaucNBDDevice *nbd_dev);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(RaucNBDDevice, free_nbd_device);

RaucNBDServer *new_nbd_server(void);
void free_nbd_server(RaucNBDServer *nbd_srv);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(RaucNBDServer, free_nbd_server);

/**
 * Configure a NBD device in the kernel using the provided parameters and
 * return the resulting device name in the struct.
 *
 * @param nbd struct with configuration
 * @param error Return location for a GError
 *
 * @return TRUE on success, FALSE if an error occurred
 */
gboolean setup_nbd_device(RaucNBDDevice *nbd_dev, GError **error);

/**
 * Remove a previously configured NBD device from the kernel.
 *
 * @param nbd struct with configuration
 * @param error Return location for a GError
 *
 * @return TRUE on success, FALSE if an error occurred
 */
gboolean remove_nbd_device(RaucNBDDevice *nbd_dev, GError **error);

gboolean nbd_server_main(gint sock, GError **error);

gboolean start_nbd_server(RaucNBDServer *nbd_srv, GError **error);
gboolean stop_nbd_server(RaucNBDServer *nbd_srv, GError **error);

gboolean nbd_read(gint sock, guint8 *data, size_t size, off64_t offset, GError **error);
