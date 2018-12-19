#include <errno.h>
#include <glib/gstdio.h>

#include "slot.h"

void r_slot_free(gpointer value)
{
	RaucSlot *slot = (RaucSlot*)value;

	g_return_if_fail(slot);

	g_free(slot->description);
	g_free(slot->device);
	g_free(slot->type);
	g_free(slot->bootname);
	g_free(slot->extra_mount_opts);
	g_free(slot->parent_name);
	g_free(slot->mount_point);
	g_free(slot->ext_mount_point);
	g_clear_pointer(&slot->status, r_slot_free_status);
	g_free(slot);
}

void r_slot_free_status(RaucSlotStatus *slotstatus)
{
	g_return_if_fail(slotstatus);

	g_free(slotstatus->bundle_compatible);
	g_free(slotstatus->bundle_version);
	g_free(slotstatus->bundle_description);
	g_free(slotstatus->bundle_build);
	g_free(slotstatus->status);
	g_free(slotstatus->checksum.digest);
	g_free(slotstatus->installed_timestamp);
	g_free(slotstatus->activated_timestamp);
	g_free(slotstatus);
}

// Something that is, or can be, mounted onto a mount point
typedef struct {
	gboolean is_device;  // vs being a loop mounted file
	dev_t dev;  // the device, or for a file, the device the file is on
	ino_t inode;  // inode of file for a non-device
} MountableObj;

// Take a device (or file) path and normalize it
static MountableObj *normalize_mountable_object(const gchar *devicepath)
{
	MountableObj *obj;
	GStatBuf st;

	if (g_stat(devicepath, &st) == -1) {
		/* Virtual filesystems like devpts trigger case */
		g_debug("Can't stat '%s', assuming unmountable: %s", devicepath, g_strerror(errno));
		return NULL;
	}

	obj = g_new0(MountableObj, 1);
	if (S_ISBLK(st.st_mode)) {
		obj->is_device = TRUE;
		obj->dev = st.st_rdev;
	} else if (S_ISREG(st.st_mode)) {
		obj->is_device = FALSE;
		obj->dev = st.st_dev;
		obj->inode = st.st_ino;
	} else {
		g_debug("Device '%s' is not something which is mountable", devicepath);
		g_clear_pointer(&obj, g_free);
	}
	return obj;
}

/* Compare two MountableObj for equality */
static gboolean is_same_mountable_object(const MountableObj *a, const MountableObj *b)
{
	return a->is_device == b->is_device && a->dev == b->dev &&
	       (a->is_device || a->inode == b->inode);
}

RaucSlot *r_slot_find_by_device(GHashTable *slots, const gchar *device)
{
	GHashTableIter iter;
	RaucSlot *slot;
	g_autofree MountableObj *obj = NULL;

	g_return_val_if_fail(slots, NULL);
	g_return_val_if_fail(device, NULL);

	obj = normalize_mountable_object(device);

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		if (g_strcmp0(slot->device, device) == 0)
			goto out;

		// Path doesn't match, but maybe device is same?
		if (obj) {
			g_autofree MountableObj *slot_obj =
				normalize_mountable_object(slot->device);

			if (slot_obj && is_same_mountable_object(obj, slot_obj))
				goto out;
		}
	}

	/* not found */
	slot = NULL;

out:
	return slot;
}

RaucSlot *r_slot_find_by_bootname(GHashTable *slots, const gchar *bootname)
{
	GHashTableIter iter;
	RaucSlot *slot;

	g_return_val_if_fail(slots, NULL);
	g_return_val_if_fail(bootname, NULL);

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &slot)) {
		if (g_strcmp0(slot->bootname, bootname) == 0) {
			goto out;
		}
	}

	slot = NULL;

out:
	return slot;
}

/* returns string representation of slot state */
gchar* r_slot_slotstate_to_str(SlotState slotstate)
{
	gchar *state = NULL;

	switch (slotstate) {
		case ST_ACTIVE:
			state = g_strdup("active");
			break;
		case ST_INACTIVE:
			state = g_strdup("inactive");
			break;
		case ST_BOOTED:
			state = g_strdup("booted");
			break;
		case ST_UNKNOWN:
		default:
			g_error("invalid slot status %d", slotstate);
			break;
	}

	return state;
}

SlotState r_slot_str_to_slotstate(gchar *str)
{
	if (g_strcmp0(str, "active") == 0) {
		return ST_ACTIVE;
	} else if (g_strcmp0(str, "inactive") == 0) {
		return ST_INACTIVE;
	} else if (g_strcmp0(str, "booted") == 0) {
		return ST_BOOTED;
	}

	return ST_UNKNOWN;
}

typedef struct {
	const gchar *name;
	gboolean mountable;
} RaucSlotType;

RaucSlotType supported_slot_types[] = {
	{"raw", FALSE},
	{"ext4", TRUE},
	{"ubifs", TRUE},
	{"ubivol", FALSE},
	{"nand", FALSE},
	{"vfat", TRUE},
	{}
};

gboolean r_slot_is_mountable(RaucSlot *slot)
{
	for (RaucSlotType *slot_type = supported_slot_types; slot_type->name != NULL; slot_type++) {
		if (g_strcmp0(slot->type, slot_type->name) == 0) {
			return slot_type->mountable;
		}
	}

	return FALSE;
}

RaucSlot* r_slot_get_parent_root(RaucSlot *slot)
{
	RaucSlot *base = NULL;

	g_return_val_if_fail(slot, NULL);

	base = slot;
	while (base != NULL && base->parent != NULL)
		base = base->parent;

	return base;
}

gchar** r_slot_get_root_classes(GHashTable *slots)
{
	GPtrArray *slotclasses = NULL;
	GHashTableIter iter;
	RaucSlot *iterslot = NULL;

	g_return_val_if_fail(slots, NULL);

	slotclasses = g_ptr_array_new();

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer*) &iterslot)) {
		const gchar *key = NULL;

		g_assert_nonnull(iterslot->sclass);

		if (iterslot->parent)
			continue;

		key = g_intern_string(iterslot->sclass);
		g_ptr_array_remove_fast(slotclasses, (gpointer)key); /* avoid duplicates */
		g_ptr_array_add(slotclasses, (gpointer)key);
	}
	g_ptr_array_add(slotclasses, NULL);

	return (gchar**) g_ptr_array_free(slotclasses, FALSE);
}

gboolean r_slot_list_contains(GList *slotlist, const RaucSlot *testslot)
{
	g_return_val_if_fail(testslot, FALSE);

	if (!slotlist)
		return FALSE;

	for (GList *l = slotlist; l != NULL; l = l->next) {
		RaucSlot *slot = l->data;

		if (slot == testslot) {
			return TRUE;
		}
	}

	return FALSE;
}

GList* r_slot_get_all_of_class(GHashTable *slots, const gchar* class)
{
	GList *retlist = NULL;
	GHashTableIter iter;
	gchar *name;
	RaucSlot *slot = NULL;

	g_return_val_if_fail(slots, NULL);
	g_return_val_if_fail(class, NULL);

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, (gpointer*) &name, (gpointer*) &slot)) {
		if (g_strcmp0(slot->sclass, class) != 0)
			continue;
		retlist = g_list_append(retlist, slot);
	}

	return retlist;
}

GList* r_slot_get_all_children(GHashTable *slots, RaucSlot *parent)
{
	GList *retlist = NULL;
	GHashTableIter iter;
	gchar *name;
	RaucSlot *slot = NULL;

	g_return_val_if_fail(slots, NULL);
	g_return_val_if_fail(parent, NULL);

	g_hash_table_iter_init(&iter, slots);
	while (g_hash_table_iter_next(&iter, (gpointer*) &name, (gpointer*) &slot)) {
		if (slot == parent)
			continue;

		if (parent != r_slot_get_parent_root(slot))
			continue;

		retlist = g_list_append(retlist, slot);
	}

	return retlist;
}
