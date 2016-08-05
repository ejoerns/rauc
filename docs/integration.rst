Integration
===========

Manual Setup
------------




Yocto
-----

Yocto support for using rauc is provided by the `meta-ptx` layer.

The layer supports building rauc both for the target and as -native tool. With
the `bundle.bbclass` it provides a mechanism to specify and build bundles directly out
of Yocto.

Target system setup
~~~~~~~~~~~~~~~~~~~

Add `meta-ptx` to your Yocto::

  git submodule add http://git-public.pengutronix.de/git-public/meta-ptx.git

Add rauc tool to your image recipe::

  IMAGE_INSTALL_append = "rauc"

Append the rauc recipe from your BSP layer (referred to as `meta-your-bsp` in the
following) by creating a ``meta-your-bsp/recipes-core/rauc/rauc_%.bbappend``
with the following content::

  FILESEXTRAPATHS_prepend := "${THISDIR}/files:"
  
  SRC_URI_append := "file://system.conf"

Write a ``system.conf`` for your board and place it in the folder yout
mentioned in the recipe (`meta-your-bsp/recipes-core/rauc/files`). 
This file must provide a system compatible to identify your system type, as
well as a represenation of all slots in your system. By default, the system
configuration will be placed in `/etc/rauc/system.conf` on your target rootfs.

For a reference about allowed configuration options in y system.conf, see
`system configuration file`_.
For a more detailed instruction on how to write a system.conf, see `chapter`_.

Bundle generation
~~~~~~~~~~~~~~~~~

Bundles can be created either manually by building and using rauc as a native
tool, or by using the ``bundle.bbclass`` that handles most of the basic steps,
automatically.

First, create a bundle recipe in your BSP layer. A possible location for this
could be ``meta-your-pbsp/recipes/core/bundles/update-bundle.bb``.

To create your bundle you first have to inherit the bundle class::

  inherit bundle

To create the manifest file, you may either use the built-in class mechanism,
or provide a self-written manifest.

For using the built-in bundle generation, you should specify some variables:

``RAUC_BUNDLE_COMPATIBLE``
  Sets the compatible string for the bundle. This should match the compatible
  your entered in your ``system.conf`` or, more general, the compatible of the
  target platform you intend to install this bundle on.

``RAUC_BUNDLE_SLOTS``
  Use this to list all slot classes your bundle should contain. A value of
  ``"rootfs appfs"`` for example will create a manifest for two slot classes;
  rootfs and appfs.

``RAUC_SLOT_<slotclass>``
  For each slotclass, set this to the image (recipe) name which build artifact
  you intend to place in it.

``RAUC_SLOT_<slotclass>[type]``
  For each slotclass, set this to the *type* of image you intend to place in this slot.
  Possible types are: ``rootfs`` (default), ``kernel``, ``bootloader``.

Based on these informations, your bundle recipe will build all components
required and generate a bundle from this. The created bundle can be found in
``tmp/deploy/images/<machine>/bundles`` in your build directory.



PTXdist
-------
   * System setup (system conf, keys, ...)
   * Bundle creation

System Boot
-----------
   * Watchdog vs. Confirmation
   * Kernel Command Line: booted slot
   * D-Bus-Service vs. Single Binary
   * Cron

Barebox
-------
   * State/Bootchooser

GRUB
----

   * Grub-Environment
   * Scripting

Backend
-------

Persistent Data
---------------

   * SSH-Keys?
