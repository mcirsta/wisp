User Interface
==============

[TOC]

Wisp is divided into a series of frontends which provide a user
interface around common core functionality. Each frontend is a
distinct implementation for a specific GUI toolkit.

Because of this the user interface has different features in
each frontend allowing the browser to be a native application.

# Loading feedback

The core measures a navigation from the start of retrieval, including connection
and server-response waiting, until the document and its frames finish loading.
The busy indicator and `Done (Xs)` status end together. Stopping a load reports
`Stopped`; a failed top-level navigation preserves its error message while the
browser displays the error page. Link and selection messages temporarily replace
the status bar text without changing the recorded completion message.

The loading cursor is an override of the requested link, text, or CSS cursor.
Completion and cancellation remove that override without requiring another
mouse event. These updates use the existing navigation events, without polling.

# Frontends

Application windows and taskbar entries always use the Wisp logo. Website
favicons belong to tabs (Qt and GTK) or the address bar (Haiku); they never
replace application branding. Navigating to another document resets its page
icon and cancels any outstanding favicon request from the previous document.

As GUI toolkits are often applicable to a single Operating
System (OS) some frontends are named for their OS instead of the
toolkit e.g. RISC OS WIMP frontend is named riscos and the Windows
win32 frontend is named windows.

## amiga

Frontend specific to the amiga

## atari

Frontend specific to the atari

## beos

Frontend specific to the Haiku OS

## framebuffer

There is a basic user guide for the [framebuffer](docs/using-framebuffer.md)

## gtk

Frontend that uses the GTK+2 or GTK+3 toolkit

## monkey

This is the internal unit test frontend.

There is a basic user guide [monkey](docs/using-monkey.md)

## riscos

Frontend for the RISC OS WIMP toolkit.

## windows

Frontend which uses the Microsodt win32 GDI toolkit.

# User configuration

The behaviour of the browser can be changed from the defaults with a
configuration file. The [core user options](docs/wisp-options.md)
of the browser are common to all versions and are augmented by each
frontend in a specific manner.
