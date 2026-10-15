/* CSSOM pseudo-element arguments, using the CSS tokenizer. MIT License. */
#include <libcss/select.h>
#include "lex/lex.h"
#include "utils/css_utils.h"
#include "utils/parserutilserror.h"

css_error css_parse_pseudo_element(const uint8_t *text, size_t length, css_pseudo_element *out)
{
    if (out == NULL || (text == NULL && length != 0))
        return CSS_BADPARM;
    static const struct { const char *name; css_pseudo_element kind; } names[] = {
        {"before", CSS_PSEUDO_ELEMENT_BEFORE}, {"after", CSS_PSEUDO_ELEMENT_AFTER},
        {"first-line", CSS_PSEUDO_ELEMENT_FIRST_LINE}, {"first-letter", CSS_PSEUDO_ELEMENT_FIRST_LETTER},
        {"marker", CSS_PSEUDO_ELEMENT_MARKER}, {"placeholder", CSS_PSEUDO_ELEMENT_PLACEHOLDER},
        {"backdrop", CSS_PSEUDO_ELEMENT_BACKDROP}, {"file-selector-button", CSS_PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON},
        {"-webkit-search-decoration", CSS_PSEUDO_ELEMENT_WEBKIT_SEARCH_DECORATION},
        {"-webkit-date-and-time-value", CSS_PSEUDO_ELEMENT_WEBKIT_DATE_AND_TIME_VALUE},
        {"-webkit-datetime-edit", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT},
        {"-webkit-datetime-edit-fields-wrapper", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_FIELDS_WRAPPER},
        {"-webkit-datetime-edit-year-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_YEAR_FIELD},
        {"-webkit-datetime-edit-month-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_MONTH_FIELD},
        {"-webkit-datetime-edit-day-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_DAY_FIELD},
        {"-webkit-datetime-edit-hour-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_HOUR_FIELD},
        {"-webkit-datetime-edit-minute-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_MINUTE_FIELD},
        {"-webkit-datetime-edit-second-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_SECOND_FIELD},
        {"-webkit-datetime-edit-millisecond-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_MILLISECOND_FIELD},
        {"-webkit-datetime-edit-meridiem-field", CSS_PSEUDO_ELEMENT_WEBKIT_DATETIME_EDIT_MERIDIEM_FIELD},
        {"-webkit-calendar-picker-indicator", CSS_PSEUDO_ELEMENT_WEBKIT_CALENDAR_PICKER_INDICATOR},
        {"-webkit-inner-spin-button", CSS_PSEUDO_ELEMENT_WEBKIT_INNER_SPIN_BUTTON},
        {"-webkit-outer-spin-button", CSS_PSEUDO_ELEMENT_WEBKIT_OUTER_SPIN_BUTTON},
        {"-webkit-scrollbar", CSS_PSEUDO_ELEMENT_WEBKIT_SCROLLBAR},
    };
    *out = CSS_PSEUDO_ELEMENT_COUNT;
    parserutils_inputstream *stream;
    css_error error = css_error_from_parserutils_error(
        parserutils_inputstream_create("UTF-8", CSS_CHARSET_DICTATED, NULL, &stream));
    if (error != CSS_OK)
        return error;
    css_lexer *lexer = NULL;
    error = css_error_from_parserutils_error(parserutils_inputstream_append(stream, text, length));
    if (error == CSS_OK)
        error = css_error_from_parserutils_error(parserutils_inputstream_append(stream, NULL, 0));
    if (error == CSS_OK)
        error = css__lexer_create(stream, &lexer);
    unsigned colons = 0;
    bool identifier = false;
    while (error == CSS_OK) {
        css_token *token;
        error = css__lexer_get_token(lexer, &token);
        if (error != CSS_OK)
            break;
        if (token->type == CSS_TOKEN_EOF) {
            if (!identifier || *out == CSS_PSEUDO_ELEMENT_COUNT)
                error = CSS_INVALID;
            break;
        }
        if (token->type == CSS_TOKEN_S && (colons == 0 || identifier))
            continue;
        if (!identifier && colons < 2 && token->type == CSS_TOKEN_CHAR && token->data.len == 1 &&
            token->data.data[0] == ':') {
            colons++;
            continue;
        }
        if (!identifier && colons > 0 && token->type == CSS_TOKEN_IDENT) {
            lwc_string *name;
            error = css_error_from_lwc_error(lwc_intern_string((const char *)token->data.data, token->data.len, &name));
            if (error != CSS_OK)
                break;
            for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
                if (css__keyword_equal(name, names[i].name) && (colons == 2 || i < 4)) {
                    *out = names[i].kind;
                    break;
                }
            }
            lwc_string_unref(name);
            identifier = true;
        } else {
            error = CSS_INVALID;
        }
    }
    if (lexer != NULL)
        css__lexer_destroy(lexer);
    parserutils_inputstream_destroy(stream);
    if (error != CSS_OK)
        *out = CSS_PSEUDO_ELEMENT_COUNT;
    return error;
}
