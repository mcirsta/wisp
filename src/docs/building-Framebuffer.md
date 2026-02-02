--------------------------------------------------------------------------------
  Build Instructions for Framebuffer Wisp                     16 March 2014
--------------------------------------------------------------------------------

  This document provides instructions for building the Framebuffer version of 
  Wisp and provides guidance on obtaining Wisp's build dependencies.

  Framebuffer Wisp has been tested on Ubuntu and Debian.

  Depending on the framebuffer frontend selected the build may need specific
  libraries installed, e.g. the SDL port requires SDL1.2 or later

  There are two ways to get Wisp building.  The QUICK-START (recommended),
  and the manual build.  Whichever you choose, you should read both the
  "Fonts", and "Selecting a frontend and appropriate options" sections below.


  Quick Start
=============

  See the QUICK-START document, which provides a simple environment with
  which you can fetch, build and install Wisp and its dependencies.

  The QUICK-START is the recommended way to build Wisp.


  Manual building
=================

  If you can't follow the quick start instructions, you will have to build
  Wisp manually.  The instructions for doing this are given below.


  Obtaining the build dependencies
----------------------------------

  Many of Wisp's dependencies are packaged on various operating systems.
  The remainder must be installed manually.  Currently, some of the libraries
  developed as part of the Wisp project have not had official releases.
  Hopefully they will soon be released with downloadable tarballs and packaged
  in common distros.  For now, you'll have to make do with Git checkouts.

  Package installation
  --------------------

  Debian-like OS:

      $ apt-get install libcurl3-dev libpng-dev

  Recent OS versions might need libcurl4-dev instead of libcurl3-dev but
  note that when it has not been built with OpenSSL, the SSL_CTX is not
  available and results that certification details won't be presented in case
  they are invalid.  But as this is currently unimplemented in the Framebuffer
  flavour of Wisp, this won't make a difference at all.

  Fedora:

      $ yum install curl-devel libpng-devel lcms-devel

  Other:

  You'll need to install the development resources for libcurl3 and libpng.


  Preparing your workspace
--------------------------

  Wisp has a number of libraries which must be built in-order and
  installed into your workspace. Each library depends on a core build
  system which Wisp projects use. This build system relies on the
  presence of things like pkg-config to find libraries and also certain
  environment variables in order to work correctly.

  Assuming you are preparing a workspace in /home/wisp/workspace then
  the following steps will set you up:

  Make the workspace directory and change to it
  ---------------------------------------------

  $ mkdir -p ${HOME}/wisp/workspace
  $ cd ${HOME}/wisp/workspace

  Make the temporary install space
  --------------------------------

  $ mkdir inst

  Make an environment script
  --------------------------
  $ cat > env.sh <<'EOF'
    export PKG_CONFIG_PATH=${HOME}/wisp/workspace/inst/lib/pkgconfig::
    export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${HOME}/wisp/workspace/inst/lib
    export PREFIX=${HOME}/wisp/workspace/inst
    EOF

  Change to workspace and source the environment
  ----------------------------------------------

  Whenever you wish to start development in a new shell, run the following:

  $ cd ${HOME}/wisp/workspace
  $ source env.sh

  From here on, any commands in this document assume you have sourced your
  shell environment.


  The Wisp project's libraries
---------------------------------

  The Wisp project has developed several libraries which are required by
  the browser. These are:

  BuildSystem     --  Shared build system, needed to build the other libraries
  LibParserUtils  --  Parser building utility functions
  LibWapcaplet    --  String internment
  Hubbub          --  HTML5 compliant HTML parser
  LibCSS          --  CSS parser and selection engine
  LibNSGIF        --  GIF format image decoder
  LibNSBMP        --  BMP and ICO format image decoder
  LibROSprite     --  RISC OS Sprite format image decoder
  LibNSFB         --  Framebuffer abstraction

  To fetch each of these libraries, run the appropriate commands from the
  Docs/LIBRARIES file.

  To build and install these libraries, simply enter each of their directories
  and run:

      $ make install

  | Note: We advise enabling iconv() support in libparserutils, which vastly
  |       increases the number of supported character sets.  To do this,
  |       create a file called Makefile.config.override in the libparserutils
  |       directory, containing the following line:
  |
  |           CFLAGS += -DWITH_ICONV_FILTER
  |
  |       For more information, consult the libparserutils README file.


  Getting the Wisp source
----------------------------

  From your workspace directory, run the following command to get the Wisp
  source:

     $ git clone git://git.netsurf-browser.org/netsurf.git

  And change to the 'wisp' directory:

     $ cd wisp


  Building and executing Wisp
--------------------------------

  First of all, you should examine the contents of Makefile.defaults
  and enable and disable relevant features as you see fit in a
  Makefile.config file.  Some of these options can be automatically
  detected and used, and where this is the case they are set to such.
  Others cannot be automatically detected from the Makefile, so you
  will either need to install the dependencies, or set them to NO.
  
  You should then obtain Wisp's dependencies, keeping in mind which options
  you have enabled in the configuration file.  See the "Obtaining Wisp's
  dependencies" section for specifics.
  
  Once done, to build Framebuffer Wisp on a UNIX-like platform, simply run:

      $ make TARGET=framebuffer

  If that produces errors, you probably don't have some of Wisp's build
  dependencies installed. See "Obtaining Wisp's dependencies" below.
  Or turn off the complaining features in your Makefile.config.  You may
  need to "make clean" before attempting to build after installing the 
  dependencies.

  Run Wisp by executing the "nsfb" program:

      $ ./nsfb

  | Note: Wisp uses certain resources at run time.  In order to find these
  |       resources, it searches three locations:
  |
  |           1. ~/.wisp/
  |           2. $WISPRES/
  |           3. /usr/share/wisp/
  |
  |       In the build tree, the resources are located at
  |
  |           framebuffer/res
  |
  |       Setting $WISPRES to point at the resources in the build tree
  |       will enable you to run Wisp from here without installation.
  |       To do this, run:
  |
  |           export WISPRES=`pwd`/framebuffer/res


  Fonts
=======

  The framebuffer port currently has two choices for font
  handling. The font handler may be selected at compile time by using
  the WISP_FB_FONTLIB configuration key. Currently supported values
  are internal and freetype

  Internal
----------

  The internal font system has a single built in monospaced face with
  CP467 encoding. The internal font plotter requires no additional
  resources and is very fast, it is also aesthetically unappealing.

  Freetype
----------

  The freetype font system (freetype version 2 API is used) has
  support for a number of different font file formats and faces. The
  framebuffer font handler takes advantage of the freetype library
  caching system to give good performance. 

  The font glyphs are, by default, rendered as 256 level transparency
  which gives excellent visual results even on small font sizes.

  The default font is the DejaVu trutype font set. The default path they
  are sourced from is /usr/share/fonts/truetype/ttf-dejavu/ . 

  The compiled in default paths may be altered by setting values in
  the user configuration makefile Makefile.config. These values must
  be set to the absolute path of the relevant font file including its
  .ttf extension. The variables are:

  WISP_FB_FONT_SANS_SERIF
  WISP_FB_FONT_SANS_SERIF_BOLD
  WISP_FB_FONT_SANS_SERIF_ITALIC
  WISP_FB_FONT_SANS_SERIF_ITALIC_BOLD
  WISP_FB_FONT_SERIF
  WISP_FB_FONT_SERIF_BOLD
  WISP_FB_FONT_MONOSPACE
  WISP_FB_FONT_MONOSPACE_BOLD
  WISP_FB_FONT_CURSIVE
  WISP_FB_FONT_FANTASY
  
  The font selection may be changed by placing truetype font files
  in the resources path. The resource files will be the generic names
  sans_serif.ttf, sans_serif_bold.ttf etc. 

  The font system is configured at runtime by several options. The
  fb_font_monochrome option causes the renderer to use monochrome
  glyph rendering which is faster to plot but slower to render and
  much less visually appealing. 

  The remaining seven options control the files to be used for font faces.

  fb_face_sans_serif - The sans serif face
  fb_face_sans_serif_bold - The bold sans serif face
  fb_face_sans_serif_italic - The italic sans serif face
  fb_face_sans_serif_italic_bold - The bold italic sans serif face.
  fb_face_serif - The serif font
  fb_serif_bold - The bold serif font
  fb_face_monospace - The monospaced font
  fb_face_monospace_bold - The bold monospaced font
  fb_face_cursive - The cursive font
  fb_face_fantasy - The fantasy font

  Old Freetype
--------------

  The framebuffer port Freetype font implementation was constructed
  using a modern version of the library (2.3.5) to use versions 2.2.1
  and prior the following patch is necessary.


Index: framebuffer/font_freetype.c
===================================================================
--- framebuffer/font_freetype.c	(revision 6750)
+++ framebuffer/font_freetype.c	(working copy)
@@ -311,6 +311,7 @@
         FT_Glyph glyph;
         FT_Error error;
         fb_faceid_t *fb_face; 
+        FTC_ImageTypeRec trec; 
 
         fb_fill_scalar(style, &srec);
 
@@ -318,15 +319,24 @@
 
         glyph_index = FTC_CMapCache_Lookup(ft_cmap_cache, srec.face_id, fb_face->cidx, ucs4);
 
-        error = FTC_ImageCache_LookupScaler(ft_image_cache, 
-                                            &srec, 
-                                            FT_LOAD_RENDER | 
-                                            FT_LOAD_FORCE_AUTOHINT | 
-                                            ft_load_type, 
-                                            glyph_index, 
-                                            &glyph, 
-                                            NULL);
 
+	trec.face_id = srec.face_id;
+	if (srec.pixel) {
+		trec.width = srec.width;
+		trec.height = srec.height;
+	} else {
+		/* Convert from 1/64 pts to pixels */
+		trec.width = srec.width * css_screen_dpi / 64 / srec.x_res;
+		trec.height = srec.height * css_screen_dpi / 64 / srec.y_res;
+	}
+	trec.flags = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT | ft_load_type;
+
+	error = FTC_ImageCache_Lookup(ft_image_cache,
+				      &trec,
+				      glyph_index,
+				      &glyph,
+				      NULL);
+
         return glyph;
 }


  Selecting a frontend and appropriate options  
==============================================  

  The framebuffer port interfaces to its input and output devices
  using the Wisp Framebuffer library (libnsfb). This library
  provides an abstraction layer to input and output devices.

  The surface used by libnsfb is selected by using the -f switch to
  Wisp when executed. A surface in this context is simply the
  combination of input and output devices.

  A surface output device may be any linearly mapped area of
  memory. The framebuffer may be treated as values at 32, 16 or 8 bits
  per pixel. The input device is typically selected to complement the
  output device and is completely specific to the surface.

  There are several configuration options which may influence the
  framebuffer surfaces. These are:

    fb_refresh - The refresh rate (for physical displays)
    fb_depth - The depth (in bits per pixel) of the framebuffer
    window_width - The width of the framebuffer
    window_height - The height of the framebuffer

  The defaults are for 800 by 600 pixels at 16bpp and 70Hz refresh rate.

  The documentation of libnsfb should be consulted for futher
  information about supported frontends and their configuration.

