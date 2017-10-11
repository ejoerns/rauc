#include <config.h>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "bundle.h"
#include "context.h"
#include "manifest.h"
#include "mount.h"
#include "signature.h"
#include "utils.h"
#include "network.h"

GQuark
r_bundle_error_quark (void)
{
  return g_quark_from_static_string ("r-bundle-error-quark");
}

static gboolean mksquashfs(const gchar *bundlename, const gchar *contentdir, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;

	r_context_begin_step("mksquashfs", "Creating squashfs", 0);

	if (g_file_test (bundlename, G_FILE_TEST_EXISTS)) {
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST, "bundle %s already exists", bundlename);
		goto out;
	}

	sproc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
				 &ierror, "mksquashfs",
				 contentdir,
				 bundlename,
				 "-all-root",
				 "-noappend",
				 "-no-progress",
				 "-no-xattrs",
				 NULL);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to start mksquashfs: ");
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to run mksquashfs: ");
		goto out;
	}

	res = TRUE;
out:
	r_context_end_step("mksquashfs", res);
	return res;
}

static gboolean unsquashfs(const gchar *bundlename, const gchar *contentdir, const gchar *extractfile, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(7, g_free);

	r_context_begin_step("unsquashfs", "Uncompressing squashfs", 0);

	g_ptr_array_add(args, g_strdup("unsquashfs"));
	g_ptr_array_add(args, g_strdup("-dest"));
	g_ptr_array_add(args, g_strdup(contentdir));
	g_ptr_array_add(args, g_strdup(bundlename));

	if (extractfile) {
		g_ptr_array_add(args, g_strdup("-e"));
		g_ptr_array_add(args, g_strdup(extractfile));
	}

	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
				 G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to start unsquashfs: ");
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to run unsquashfs: ");
		goto out;
	}

	res = TRUE;
out:
	r_context_end_step("unsquashfs", res);
	return res;
}

static gboolean casync_make(const gchar *bundlename, const gchar *contentdir, const gchar *store, GError **error) {
	GSubprocess *sproc = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	GPtrArray *args = g_ptr_array_new_full(5, g_free);

	g_ptr_array_add(args, g_strdup("casync"));
	g_ptr_array_add(args, g_strdup("make"));
	g_ptr_array_add(args, g_strdup(bundlename));
	g_ptr_array_add(args, g_strdup(contentdir));
	if (store) {
		g_ptr_array_add(args, g_strdup("--store"));
		g_ptr_array_add(args, g_strdup(store));
	}
	g_ptr_array_add(args, NULL);

	sproc = g_subprocess_newv((const gchar * const *)args->pdata,
				 G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
	if (sproc == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to start casync: ");
		goto out;
	}

	res = g_subprocess_wait_check(sproc, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed to run casync: ");
		goto out;
	}

	res = TRUE;
out:
	return res;
}

static gboolean output_stream_write_uint64_all(GOutputStream *stream,
                                              guint64 data,
                                              GCancellable *cancellable,
                                              GError **error)
{
	gsize bytes_written;
	gboolean res;

	data = GUINT64_TO_BE(data);
	res = g_output_stream_write_all(stream, &data, sizeof(data), &bytes_written,
					 cancellable, error);
	g_assert(bytes_written == sizeof(data));
	return res;
}

static gboolean input_stream_read_uint64_all(GInputStream *stream,
                                             guint64 *data,
                                             GCancellable *cancellable,
                                             GError **error)
{
	guint64 tmp;
	gsize bytes_read;
	gboolean res;

	res = g_input_stream_read_all(stream, &tmp, (goffset) sizeof(tmp), &bytes_read,
		                      cancellable, error);
	g_assert(bytes_read == sizeof(tmp));
	*data = GUINT64_FROM_BE(tmp);
	return res;
}

#define SQUASHFS_MAGIC			0x73717368
#define RAUC_CASYNC_MAGIC		0xa31434e53414352

static gboolean input_stream_read_bundle_identifier(GInputStream *stream, BundleType *type, GError **error) {
	GError *ierror = NULL;
	guint32 squashfs_id;
	guint64 rauc_casync_id;
	gboolean res;
	gsize bytes_read;

	res = g_input_stream_read_all(stream, &squashfs_id, (goffset) sizeof(squashfs_id), &bytes_read, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (bytes_read != sizeof(squashfs_id)) {
		g_set_error(error,
				G_IO_ERROR,
				G_IO_ERROR_PARTIAL_INPUT,
				"Only %lu of %lu bytes read",
				bytes_read,
				sizeof(squashfs_id));
		return FALSE;
	}

	res = g_seekable_seek(G_SEEKABLE(stream), -(goffset) sizeof(squashfs_id), G_SEEK_CUR, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		return FALSE;
	}

	if (squashfs_id == SQUASHFS_MAGIC) {
		g_debug("Detected SquashFS Bundle identifier");
		if (type)
			*type = BUNDLE_SQUASHFS;
		return TRUE;
	}

	res = g_input_stream_read_all(stream, &rauc_casync_id, (goffset) sizeof(rauc_casync_id), &bytes_read, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		return FALSE;
	}
	if (bytes_read != sizeof(rauc_casync_id)) {
		g_set_error(error,
				G_IO_ERROR,
				G_IO_ERROR_PARTIAL_INPUT,
				"Only %lu of %lu bytes read",
				bytes_read,
				sizeof(rauc_casync_id));
		return FALSE;
	}

	if (rauc_casync_id == RAUC_CASYNC_MAGIC) {
		g_debug("Detected RAUC casync bundle identifier");
		if (type)
			*type = BUNDLE_CASYNC;
		return TRUE;
	}

	if (type)
		*type = BUNDLE_UNKNOWN;

	return TRUE;
}

static gboolean output_stream_write_bytes_all(GOutputStream *stream,
                                              GBytes *bytes,
                                              GCancellable *cancellable,
                                              GError **error)
{
	const void *buffer;
	gsize count, bytes_written;

	buffer = g_bytes_get_data(bytes, &count);
	return g_output_stream_write_all(stream, buffer, count, &bytes_written,
					 cancellable, error);
}

static gboolean input_stream_read_bytes_all(GInputStream *stream,
		                            GBytes **bytes,
                                            gsize count,
                                            GCancellable *cancellable,
                                            GError **error)
{
	void *buffer = NULL;
	gsize bytes_read;
	gboolean res;

	g_assert_cmpint(count, !=, 0);

	buffer = g_malloc0(count);

	res = g_input_stream_read_all(stream, buffer, count, &bytes_read,
		                      cancellable, error);
	if (!res) {
		g_free(buffer);
		return res;
	}
	g_assert(bytes_read == count);
	*bytes = g_bytes_new_take(buffer, count);
	return TRUE;
}

static gboolean sign_bundle(const gchar *bundlename, GError **error) {
	GError *ierror = NULL;
	GBytes *sig = NULL;
	GFile *bundlefile = NULL;
	GFileOutputStream *bundlestream = NULL;
	gboolean res = FALSE;
	guint64 offset;

	g_assert_nonnull(r_context()->certpath);
	g_assert_nonnull(r_context()->keypath);

	sig = cms_sign_file(bundlename,
			    r_context()->certpath,
			    r_context()->keypath,
			    &ierror);
	if (sig == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed signing bundle: ");
		res = FALSE;
		goto out;
	}

	bundlefile = g_file_new_for_path(bundlename);
	bundlestream = g_file_append_to(bundlefile, G_FILE_CREATE_NONE, NULL, &ierror);
	if (bundlestream == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to open bundle for appending: ");
		res = FALSE;
		goto out;
	}

	res = g_seekable_seek(G_SEEKABLE(bundlestream),
			      0, G_SEEK_END, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to seek to end of bundle: ");
		goto out;
	}

	offset = g_seekable_tell((GSeekable *)bundlestream);
	res = output_stream_write_bytes_all((GOutputStream *)bundlestream, sig, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to append signature to bundle: ");
		goto out;
	}


	offset = g_seekable_tell((GSeekable *)bundlestream) - offset;
	res = output_stream_write_uint64_all((GOutputStream *)bundlestream, offset, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to append signature size to bundle: ");
		goto out;
	}

out:
	g_clear_object(&bundlestream);
	g_clear_object(&bundlefile);
	g_clear_pointer(&sig, g_bytes_unref);
	return res;
}

gboolean create_bundle(const gchar *bundlename, const gchar *contentdir, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	res = mksquashfs(bundlename, contentdir, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = sign_bundle(bundlename, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	return res;
}

static gboolean truncate_bundle(const gchar *inpath, const gchar *outpath, gsize size, GError **error) {
	GFile *infile, *outfile = NULL;
	GFileInputStream *instream = NULL;
	GFileOutputStream *outstream = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;
	gssize ssize;

	infile = g_file_new_for_path(inpath);
	outfile = g_file_new_for_path(outpath);

	instream = g_file_read(infile, NULL, &ierror);
	if (instream == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to open bundle for reading: ");
		res = FALSE;
		goto out;
	}
	outstream = g_file_create(outfile, G_FILE_CREATE_NONE, NULL,
			&ierror);
	if (outstream == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to open bundle for writing: ");
		res = FALSE;
		goto out;
	}

	ssize = g_output_stream_splice(
			(GOutputStream*)outstream,
			(GInputStream*)instream,
			G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
			NULL, &ierror);
	if (ssize == -1) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}

	res = g_seekable_truncate(G_SEEKABLE(outstream), size, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	g_clear_object(&outstream);
	g_clear_object(&infile);
	g_clear_object(&outfile);
	return res;
}

gboolean resign_bundle(RaucBundle *bundle, const gchar *outpath, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail(bundle != NULL, FALSE);

	res = truncate_bundle(bundle->path, outpath, bundle->size, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = sign_bundle(outpath, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	return res;
}

static gboolean convert_to_casync_bundle(RaucBundle *bundle, const gchar *outbundle, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;
	gchar *tmpdir, *contentdir, *mfpath, *bundleidxpath, *storepath, *basepath;
	RaucManifest *mf = NULL;
	GFile *bundlefile = NULL, *manifestfile = NULL, *caidxfile = NULL;
	GFileOutputStream *bundlestream = NULL;
	gssize writesize;
	GInputStream *manifeststream, *idxinstream = NULL;

	g_return_val_if_fail(bundle, FALSE);
	g_return_val_if_fail(outbundle, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	basepath = g_strndup(outbundle, strlen(outbundle) - 6);
	bundleidxpath = g_strconcat(basepath, ".caidx", NULL);
	storepath = g_strconcat(basepath, ".castr", NULL);
	g_free(basepath);

	tmpdir = g_build_filename(g_get_tmp_dir(), "_rauc_casync", NULL);
	contentdir = g_build_filename(tmpdir, "content", NULL);
	mfpath = g_build_filename(contentdir, "manifest.raucm", NULL);

	if (g_mkdir(tmpdir, 0777) != 0) {
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Failed to create tmp dir %s", tmpdir);
		res = FALSE;
		goto out;
	}

	/* Extract input bundle to content/ dir */
	res = extract_bundle(bundle, contentdir, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	/* Load manifest from content/ dir */
	res = load_manifest_file(mfpath, &mf, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	/* Generate catar for content dir */
	res = casync_make(bundleidxpath, contentdir, storepath, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	/* Open bundle file */
	bundlefile = g_file_new_for_path(outbundle);
	bundlestream = g_file_create(bundlefile, G_FILE_CREATE_NONE, NULL, &ierror);
	if (bundlestream == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to open bundle for writing: ");
		res = FALSE;
		goto out;
	}

	/* Write casync bundle identifier */
	res = g_output_stream_write_all(G_OUTPUT_STREAM(bundlestream), "RCASNC1\n", 8, NULL, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	/* Append manifest content */
	manifestfile = g_file_new_for_path(mfpath);
	manifeststream = (GInputStream*)g_file_read(manifestfile, NULL, &ierror);

	writesize = g_output_stream_splice(G_OUTPUT_STREAM(bundlestream),
			manifeststream, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
			NULL, &ierror);
	if (writesize == -1) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}

	/* Append caidx content */
	caidxfile = g_file_new_for_path(bundleidxpath);
	idxinstream = (GInputStream*)g_file_read(caidxfile, NULL, &ierror);

	writesize = g_output_stream_splice(G_OUTPUT_STREAM(bundlestream),
			idxinstream, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
			NULL, &ierror);
	if (writesize == -1) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}
	
	/* Append caidx size */
	res = output_stream_write_uint64_all((GOutputStream *)bundlestream, writesize, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to append caidx size to bundle: ");
		goto out;
	}


	res = TRUE;
out:
	/* Remove temporary bundle creation directory */
	rm_tree(tmpdir, NULL);

	g_clear_pointer(&mf, free_manifest);
	g_clear_pointer(&tmpdir, g_free);
	g_clear_pointer(&contentdir, g_free);
	g_clear_pointer(&mfpath, g_free);
	g_clear_pointer(&bundleidxpath, g_free);
	g_clear_pointer(&storepath, g_free);
	g_clear_object(&bundlestream);
	g_clear_object(&caidxfile);
	return res;
}

gboolean create_casync_bundle(RaucBundle *bundle, const gchar *outbundle, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	res = convert_to_casync_bundle(bundle, outbundle, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = sign_bundle(outbundle, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	return res;
}

gboolean check_bundle(const gchar *bundlename, RaucBundle **bundle, gboolean verify, GError **error) {
	GError *ierror = NULL;
	GBytes *sig = NULL;
	GFile *bundlefile = NULL;
	GFileInputStream *bundlestream = NULL;
	guint64 sigsize;
	goffset offset;
	gboolean res = FALSE;
	RaucBundle *ibundle = g_new0(RaucBundle, 1);
	BundleType btype;

	g_return_val_if_fail (bundle == NULL || *bundle == NULL, FALSE);

	ibundle->path = g_strdup(bundlename);

	r_context_begin_step("check_bundle", "Checking bundle", verify);

	if (verify && !r_context()->config->keyring_path) {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_KEYRING, "No keyring file provided");
		goto out;
	}

	g_message("Reading bundle: %s", bundlename);

	bundlefile = g_file_new_for_path(bundlename);
	bundlestream = g_file_read(bundlefile, NULL, &ierror);
	if (bundlestream == NULL) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to open bundle for reading: ");
		goto out;
	}

	res = input_stream_read_bundle_identifier(G_INPUT_STREAM(bundlestream), &btype, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to read bundle identifier: ");
		goto out;
	}

	if (btype == BUNDLE_UNKNOWN) {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_IDENTIFIER, "invalid bundle identifier");
		res = FALSE;
		goto out;
	}

	ibundle->type = btype;

	offset = sizeof(sigsize);
	res = g_seekable_seek(G_SEEKABLE(bundlestream),
			      -offset, G_SEEK_END, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to seek to end of bundle: ");
		goto out;
	}
	offset = g_seekable_tell((GSeekable *)bundlestream);

	res = input_stream_read_uint64_all(G_INPUT_STREAM(bundlestream),
			                   &sigsize, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to read signature size from bundle: ");
		goto out;
	}

	if (sigsize == 0) {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
				"signature size is 0");
		res = FALSE;
		goto out;
	}
	/* sanity check: signature should be smaller than bundle size */
	if (sigsize > (guint64)offset) {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
				"signature size (%"G_GUINT64_FORMAT") exceeds bundle size", sigsize);
		res = FALSE;
		goto out;
	}
	/* sanity check: signature should be smaller than 64kiB */
	if (sigsize > 0x4000000) {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
				"signature size (%"G_GUINT64_FORMAT") exceeds 64KiB", sigsize);
		res = FALSE;
		goto out;
	}

	offset -= sigsize;

	ibundle->size = offset;

	res = g_seekable_seek(G_SEEKABLE(bundlestream),
			      offset, G_SEEK_SET, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to seek to start of bundle signature: ");
		goto out;
	}

	res = input_stream_read_bytes_all(G_INPUT_STREAM(bundlestream),
			                  &sig, sigsize, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to read signature from bundle: ");
		goto out;
	}

	if (verify) {
		CMS_ContentInfo *cms = NULL;
		X509_STORE *store = NULL;

		g_message("Verifying bundle... ");
		/* the squashfs image size is in offset */
		res = cms_verify_file(bundlename, sig, offset, &cms, &store, &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}

		res = cms_get_cert_chain(cms, store, &ibundle->verified_chain, &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}

		X509_STORE_free(store);
		CMS_ContentInfo_free(cms);
	}

	if (bundle)
		*bundle = ibundle;

	res = TRUE;
out:
	if (!bundle)
		free_bundle(ibundle);
	g_clear_object(&bundlestream);
	g_clear_object(&bundlefile);
	g_clear_pointer(&sig, g_bytes_unref);
	r_context_end_step("check_bundle", res);
	return res;
}

gboolean extract_bundle(RaucBundle *bundle, const gchar *outputdir, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail(bundle != NULL, FALSE);

	r_context_begin_step("extract_bundle", "Extracting bundle", 1);

	g_debug("extracting bundle content to %s", outputdir);

	if (bundle->type == BUNDLE_SQUASHFS) {
		res = unsquashfs(bundle->path, outputdir, NULL, &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}
	} else if (bundle->type == BUNDLE_CASYNC) {
		g_warning("CASYNC BUNDLE NOT SUPPORTED, YET!");
	} else {
		g_warning("UNKNOWN BUNDLE TYPE!!!");
	}


	res = TRUE;
out:
	r_context_end_step("extract_bundle", res);
	return res;
}

typedef enum {
	BUNDLE_CASYNC_READ_MANIFEST,
	BUNDLE_CASYNC_READ_IDX
} BundleCasyncRead;

static GBytes* read_casync_bundle(const gchar *bundlename, BundleCasyncRead readtype, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;
	GFile *bundlefile = NULL;
	GInputStream *bundlestream = NULL;
	guint64 sigsize;
	goffset offset;
	GBytes *mfdata = NULL;

	bundlefile = g_file_new_for_path(bundlename);
	bundlestream = (GInputStream*)g_file_read(bundlefile, NULL, &ierror);

	/* seek to end - signature size offset */
	offset = sizeof(sigsize);
	res = g_seekable_seek(G_SEEKABLE(bundlestream),
			-offset, G_SEEK_END, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to seek to end of bundle: ");
		goto out;
	}

	/* get sigsize */
	res = input_stream_read_uint64_all(G_INPUT_STREAM(bundlestream),
			&sigsize, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to read signature size from bundle: ");
		goto out;
	}

	/* Seek to start of signature */
	res = g_seekable_seek(G_SEEKABLE(bundlestream),
			-sigsize - 2 * sizeof(sigsize), G_SEEK_CUR, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to seek to end of caidx: ");
		goto out;
	}

	/* get caidx size */
	res = input_stream_read_uint64_all(G_INPUT_STREAM(bundlestream),
			&sigsize, NULL, &ierror);
	if (!res) {
		g_propagate_prefixed_error(
				error,
				ierror,
				"failed to read caidx size from bundle: ");
		goto out;
	}

	offset = g_seekable_tell((GSeekable *)bundlestream);

	if (readtype == BUNDLE_CASYNC_READ_MANIFEST) {
		sigsize = offset - sizeof(sigsize) - sigsize - 8;

		/* Seek to start of manifest */
		res = g_seekable_seek(G_SEEKABLE(bundlestream),
				8, G_SEEK_SET, NULL, &ierror);
		if (!res) {
			g_propagate_prefixed_error(
					error,
					ierror,
					"failed to seek to start of manifest: ");
			goto out;
		}
	} else if (readtype == BUNDLE_CASYNC_READ_IDX) {
		/* Seek to start of idx */
		res = g_seekable_seek(G_SEEKABLE(bundlestream),
				-sigsize - sizeof(sigsize), G_SEEK_CUR, NULL, &ierror);
		if (!res) {
			g_propagate_prefixed_error(
					error,
					ierror,
					"failed to seek to start of manifest: ");
			goto out;
		}
	} else {
		/* Should not be reached. Abort here! */
		g_error("Invalid readtype");
	}

	res = input_stream_read_bytes_all(bundlestream, &mfdata, sigsize, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}

	g_clear_object(&bundlestream);
	g_clear_object(&bundlefile);
out:
	return mfdata;
}

static gboolean is_remote_scheme(const gchar *scheme) {
	return (g_strcmp0(scheme, "http") == 0) ||
		(g_strcmp0(scheme, "https") == 0) ||
		(g_strcmp0(scheme, "sftp") == 0) ||
		(g_strcmp0(scheme, "ftp") == 0);
}

gboolean extract_manifest_from_bundle(RaucBundle *bundle, GError **error) {
	gchar *bundlescheme, *origpath = NULL;
	gchar* manifestpath = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail(bundle != NULL, FALSE);

	/* Download Bundle to temporary location if remote URI is given */
	bundlescheme = g_uri_parse_scheme(bundle->path);
	if (is_remote_scheme(bundlescheme)) {
#if ENABLE_NETWORK
		g_message("Remote URI detected, downloading bundle ...");
		origpath = bundle->path;
		bundle->path = g_build_filename(g_get_tmp_dir(), "_download.raucb", NULL);
		res = download_file(bundle->path, origpath, 64*1024, &ierror);
		if (!res) {
			g_propagate_prefixed_error(error, ierror, "Failed to download bundle %s: ", origpath);
			goto out;
		}
		g_debug("Downloaded temp bundle to %s", bundle->path);
#else
		g_warning("Mounting remote bundle not supported, recompile with --enable-network");
#endif
	}


	if (bundle->type == BUNDLE_SQUASHFS) {
		gchar* tmpdir = NULL;
		gchar* bundledir = NULL;

		tmpdir = g_dir_make_tmp("bundle-XXXXXX", &ierror);
		if (!tmpdir) {
			g_propagate_error(error, ierror);
			goto out;
		}
		bundledir = g_build_filename(tmpdir, "bundle-content", NULL);
		manifestpath = g_build_filename(bundledir, "manifest.raucm", NULL);

		res = unsquashfs(bundle->path, bundledir, "manifest.raucm", &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}

		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}

		res = load_manifest_file(manifestpath, &bundle->manifest, &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}

	} else if (bundle->type == BUNDLE_CASYNC) {
		GBytes *mfdata = NULL;

		mfdata = read_casync_bundle(bundle->path, BUNDLE_CASYNC_READ_MANIFEST, &ierror);
		if (!mfdata) {
			g_propagate_error(error, ierror);
			res = FALSE;
			goto out;
		}

		res = load_manifest_mem(mfdata, &bundle->manifest, &ierror);
		if (!res) {
			g_propagate_error(error, ierror);
			goto out;
		}
	} else {
		g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNKNOWN_FORMAT, "Unknown bundle format %d", bundle->type);
		res = FALSE;
		goto out;
	}

	res = TRUE;

out:
	/* In case of remote bundle, cleanup locally downloaded one */
	if (origpath) {
		g_remove(bundle->path);
	}

	return res;
}

static gboolean mount_bundle_classic(RaucBundle *bundle, const gchar *mountpoint, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	res = r_mount_loop(bundle->path, mountpoint, bundle->size, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	return res;
}

static gboolean mount_bundle_casync(RaucBundle *bundle, const gchar *mountpoint, const gchar* storepath, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;
	GBytes *mfdata = NULL;
	GFile *tmpidx = NULL;
	GFileIOStream *iostream = NULL;
	GOutputStream *tmpidxstream = NULL;
	GPtrArray *seeds = g_ptr_array_new_full(2, g_free);

	/* extract idx file from bundle */
	mfdata = read_casync_bundle(bundle->path, BUNDLE_CASYNC_READ_IDX, &ierror);
	if (!mfdata) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}

	/* Write to temporary location */
	tmpidx = g_file_new_tmp("XXXXXX.caidx", &iostream, &ierror);
	if (!tmpidx) {
		g_propagate_error(error, ierror);
		res = FALSE;
		goto out;
	}

	tmpidxstream = g_io_stream_get_output_stream((GIOStream*)iostream);

	res = output_stream_write_bytes_all(tmpidxstream, mfdata, NULL, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}
	g_clear_object(&tmpidxstream);

	g_debug("Wrote idx to %s, using store path: %s", g_file_get_path(tmpidx), storepath);

	{
		GHashTableIter iter;
		RaucSlot *iterslot = NULL;

		/* iterate over all active slots */
		g_hash_table_iter_init(&iter, r_context()->config->slots);
		while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&iterslot)) {

			if (!(iterslot->state & ST_ACTIVE))
				continue;

			/* if there is an image for it in the bundle, take slot as seed */
			for (GList *l = bundle->manifest->images; l != NULL; l = l->next) {
				RaucImage *image = l->data;

				if (g_str_equal(iterslot->sclass, image->slotclass)) {
					/* FIXME: we need to add the slots
					 * mount point here if casync uses
					 * diectories! */
					g_debug("Adding as casync seed: %s", iterslot->device);
					g_ptr_array_add(seeds, g_strdup(iterslot->device));
					break;
				}
			}
		}
		g_ptr_array_add(seeds, NULL);
	}

	res = r_mount_casync(g_file_get_path(tmpidx), mountpoint, storepath, (gchar**) seeds->pdata, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	res = TRUE;
out:
	g_ptr_array_free(seeds, TRUE);
	g_clear_object(&tmpidx);
	return res;
}

gboolean mount_bundle(RaucBundle *bundle, GError **error) {
	gchar *mount_point= NULL, *bundlescheme = NULL, *origpath = NULL, *bundlename = NULL;
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail(bundle != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_assert_null(bundle->mount_point);

	mount_point = r_create_mount_point("bundle", &ierror);
	if (!mount_point) {
		res = FALSE;
		g_propagate_prefixed_error(
				error,
				ierror,
				"Failed creating mount point: ");
		goto out;
	}

	g_message("Mounting bundle '%s' to '%s'", bundle->path, mount_point);

	/* Download Bundle to temporary location if remote URI is given */
	bundlescheme = g_uri_parse_scheme(bundle->path);
	if (is_remote_scheme(bundlescheme)) {
#if ENABLE_NETWORK
		g_message("Remote URI detected, downloading bundle ...");
		origpath = bundle->path;
		bundlename = g_build_filename(g_get_tmp_dir(), "_download.raucb", NULL);
		res = download_file(bundlename, origpath, 64*1024, &ierror);
		if (!res) {
			g_propagate_prefixed_error(error, ierror, "Failed to download bundle %s: ", origpath);
			goto out;
		}
		g_debug("Downloaded temp bundle to %s", bundlename);
#else
		g_warning("Mounting remote bundle not supported, recompile with --enable-network");
#endif
	}

	if (bundle->type == BUNDLE_SQUASHFS) {
		res = mount_bundle_classic(bundle, mount_point, &ierror);
	} else if (bundle->type == BUNDLE_CASYNC) {
		gchar *storepath;

		g_warning("Mounting casync bundle not fully suppoted, yet");
		if (origpath)
			storepath = g_strndup(origpath, strlen(origpath) - 6);
		else
			storepath = g_strndup(bundle->path, strlen(bundle->path) - 6);
		/* storepath can be overridden by system configuration */
		if (r_context()->config->store_prefix)
			storepath = r_context()->config->store_prefix;
		storepath = g_strconcat(storepath, ".castr", NULL);

		res = mount_bundle_casync(bundle, mount_point, storepath, &ierror);

		g_clear_pointer(&storepath, g_free);
	} else {
		/* Should not be reached! Abort here */
		g_error("Cannot mount unknown bundle type!");
	}
	if (!res) {
		g_propagate_error(error, ierror);
		g_rmdir(mount_point);
		g_free(mount_point);
		goto out;
	}

	bundle->mount_point = mount_point;

	res = TRUE;
out:
	/* In case of remote bundle, cleanup locally downloaded one */
	if (origpath) {
		g_remove(bundlename);
	}

	g_clear_pointer(&origpath, g_free);
	g_clear_pointer(&bundlescheme, g_free);
	return res;
}

gboolean umount_bundle(RaucBundle *bundle, GError **error) {
	GError *ierror = NULL;
	gboolean res = FALSE;

	g_return_val_if_fail(bundle != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_assert_nonnull(bundle->mount_point);

	res = r_umount(bundle->mount_point, &ierror);
	if (!res) {
		g_propagate_error(error, ierror);
		goto out;
	}

	g_rmdir(bundle->mount_point);
	g_clear_pointer(&bundle->mount_point, g_free);

	res = TRUE;
out:
	return res;
}

void free_bundle(RaucBundle *bundle) {
	g_return_if_fail(bundle);

	g_free(bundle->path);
	g_free(bundle->mount_point);
	if (bundle->verified_chain)
		sk_X509_pop_free(bundle->verified_chain, X509_free);
	if (bundle->manifest)
		free_manifest(bundle->manifest);
	g_free(bundle);
}
