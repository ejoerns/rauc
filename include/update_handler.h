#pragma once

#include "manifest.h"
#include <glib.h>

typedef gboolean (*img_to_fs_handler) (RaucImage *image, RaucSlot *dest_slot, GError **error);

gboolean register_fstype_handler(const gchar* fstype, const gchar* imgtype, img_to_fs_handler handler);

void setup_fstype_handlers(void);

img_to_fs_handler check_for_update_handler(RaucImage *mfimage, RaucSlot  *dest_slot, GError **error);
