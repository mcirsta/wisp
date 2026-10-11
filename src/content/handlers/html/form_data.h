/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WISP_HTML_FORM_DATA_H
#define WISP_HTML_FORM_DATA_H

#include <dom/dom.h>
#include <wisp/utils/errors.h>

struct fetch_multipart_data;

/* Owned scalar UTF-8 values, before transport encoding or newline conversion.
 * A file entry holds its display filename separately from its local path.
 * This is the native upload payload, not a script-visible Blob/File object. */
struct html_form_entry {
    dom_string *name;
    dom_string *value; /* String value, or the file's display filename. */
    char *file_path; /* NULL for strings, "" for an empty file selection. */
};

struct html_form_data {
    struct html_form_entry *entries;
    size_t length;
    size_t capacity;
};

/* NULL strings mean empty strings. Inputs are borrowed; a successful append
 * retains/copies its payload. Failure leaves the existing list unchanged. */
nserror html_form_data_append(struct html_form_data *data, dom_string *name, dom_string *value);
nserror
html_form_data_append_file(struct html_form_data *data, dom_string *name, dom_string *filename, const char *path);
void html_form_data_fini(struct html_form_data *data);

/* Built-in control extraction; no callbacks or encoding. Starts a new output;
 * callers must finish a previous result before reuse. Empty result on failure.
 * encoding is the canonical submission encoding; NULL means UTF-8 (FormData).
 * FormDataEvent, reentrancy and full File/Blob storage are separate prerequisites. */
nserror html_form_data_construct(
    dom_html_form_element *form, dom_node *submitter, const char *encoding, struct html_form_data *data);

/* HTML's ordered accept-charset lookup, then output-encoding conversion.
 * NULL accept uses the document encoding; empty/unknown labels use UTF-8.
 * Returns a static canonical name without allocating or accessing the DOM. */
const char *html_form_submission_encoding(dom_string *accept, const char *document_encoding);

/* Does not mutate the original Unicode entries. Output is owned by the caller.
 * encoding is a canonical output name from html_form_submission_encoding(). */
nserror html_form_data_urlencode(const struct html_form_data *data, const char *encoding, char **output);
nserror
html_form_data_multipart(const struct html_form_data *data, const char *encoding, struct fetch_multipart_data **output);

#endif
