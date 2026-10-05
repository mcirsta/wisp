/* SPDX-License-Identifier: MIT */
#include "select/scope.h"
#include "parse/propstrings.h"

static bool named(const css_stylesheet *sheet, const css_selector_detail *detail, unsigned name)
{
    bool equal = false;
    return lwc_string_caseless_isequal(detail->qname.name, sheet->propstrings[name], &equal) == lwc_error_ok && equal;
}

static bool local_pseudo(const css_stylesheet *sheet, const css_selector_detail *detail)
{
    /* Keep an explicit allowlist: parser acceptance alone does not prove that a
     * future relational pseudo stays within the changed node's parent subtree.
     * :not() currently becomes one negated simple detail, so uses the same test. */
    static const unsigned classes[] = {FIRST_CHILD, LAST_CHILD, FIRST_OF_TYPE, LAST_OF_TYPE, ONLY_CHILD,
        ONLY_OF_TYPE, ROOT, EMPTY, LINK, VISITED, HOVER, ACTIVE, FOCUS, TARGET, ENABLED, DISABLED, CHECKED};
    static const unsigned elements[] = {FIRST_LINE, FIRST_LETTER, BEFORE, AFTER, BACKDROP, FILE_SELECTOR_BUTTON,
        PLACEHOLDER, MARKER, WEBKIT_SEARCH_DECORATION, WEBKIT_DATE_AND_TIME_VALUE, WEBKIT_DATETIME_EDIT,
        WEBKIT_DATETIME_EDIT_FIELDS_WRAPPER, WEBKIT_DATETIME_EDIT_YEAR_FIELD, WEBKIT_DATETIME_EDIT_MONTH_FIELD,
        WEBKIT_DATETIME_EDIT_DAY_FIELD, WEBKIT_DATETIME_EDIT_HOUR_FIELD, WEBKIT_DATETIME_EDIT_MINUTE_FIELD,
        WEBKIT_DATETIME_EDIT_SECOND_FIELD, WEBKIT_DATETIME_EDIT_MILLISECOND_FIELD, WEBKIT_DATETIME_EDIT_MERIDIEM_FIELD,
        WEBKIT_CALENDAR_PICKER_INDICATOR, WEBKIT_INNER_SPIN_BUTTON, WEBKIT_OUTER_SPIN_BUTTON, WEBKIT_SCROLLBAR};

    if (detail->type == CSS_SELECTOR_PSEUDO_CLASS) {
        if (named(sheet, detail, NTH_CHILD) || named(sheet, detail, NTH_LAST_CHILD)) {
            /* The current 'of' grammar stores one class name, not an arbitrary
             * selector. Extending it requires revisiting this proof. */
            return detail->value_type == CSS_SELECTOR_DETAIL_VALUE_NTH ||
                (detail->value_type == CSS_SELECTOR_DETAIL_VALUE_NTH_OF && detail->value.nth_of.filter_class != NULL);
        }
        if (named(sheet, detail, NTH_OF_TYPE) || named(sheet, detail, NTH_LAST_OF_TYPE))
            return detail->value_type == CSS_SELECTOR_DETAIL_VALUE_NTH;
        /* HTML directionality can read descendants, but checkedness and CSS
         * changes do not alter it. DOM text/attribute/value edits still require
         * the caller's general invalidation, outside this checked-only proof. */
        if (named(sheet, detail, LANG) || named(sheet, detail, DIR))
            return detail->value_type == CSS_SELECTOR_DETAIL_VALUE_STRING && detail->value.string != NULL;
    }
    if (detail->value_type != CSS_SELECTOR_DETAIL_VALUE_STRING || detail->value.string != NULL)
        return false;

    const unsigned *names = detail->type == CSS_SELECTOR_PSEUDO_CLASS ? classes : elements;
    size_t count = detail->type == CSS_SELECTOR_PSEUDO_CLASS ? sizeof(classes) / sizeof(classes[0]) :
                                                            sizeof(elements) / sizeof(elements[0]);
    for (size_t i = 0; i < count; i++) {
        if (named(sheet, detail, names[i]))
            return true;
    }
    return false;
}

bool css__selector_checked_parent_scope(const css_stylesheet *sheet, const css_selector *selector)
{
    for (; selector != NULL; selector = selector->combinator) {
        /* From a checked compound toward the subject, these relationships only
         * reach descendants or following siblings, all inside its parent. This
         * includes horizontal edges even when a selector has no :checked yet. */
        switch (selector->data.comb) {
        case CSS_COMBINATOR_NONE:
        case CSS_COMBINATOR_ANCESTOR:
        case CSS_COMBINATOR_PARENT:
        case CSS_COMBINATOR_SIBLING:
        case CSS_COMBINATOR_GENERIC_SIBLING:
            break;
        default:
            return false;
        }
        const css_selector_detail *detail = &selector->data;
        do {
            switch (detail->type) {
            case CSS_SELECTOR_ELEMENT:
            case CSS_SELECTOR_CLASS:
            case CSS_SELECTOR_ID:
            case CSS_SELECTOR_ATTRIBUTE:
            case CSS_SELECTOR_ATTRIBUTE_EQUAL:
            case CSS_SELECTOR_ATTRIBUTE_DASHMATCH:
            case CSS_SELECTOR_ATTRIBUTE_INCLUDES:
            case CSS_SELECTOR_ATTRIBUTE_PREFIX:
            case CSS_SELECTOR_ATTRIBUTE_SUFFIX:
            case CSS_SELECTOR_ATTRIBUTE_SUBSTRING:
                if (detail->value_type != CSS_SELECTOR_DETAIL_VALUE_STRING)
                    return false;
                break;
            case CSS_SELECTOR_PSEUDO_CLASS:
            case CSS_SELECTOR_PSEUDO_ELEMENT:
                if (!local_pseudo(sheet, detail))
                    return false;
                break;
            default:
                return false;
            }
        } while ((detail++)->next);
    }
    return true;
}

static bool sheet_scope(const css_stylesheet *sheet, unsigned depth, uint64_t *revision)
{
    /* Bound malformed/cyclic import graphs without allocating a visited set. */
    if (sheet == NULL || sheet->parser != NULL || sheet->checked_scope_unsafe || depth == 64)
        return false;
    if (sheet->revision > *revision)
        *revision = sheet->revision;
    /* Imports are confined to this leading rule group by the parser, as in
     * selection. Local rules were summarized at insertion, not per update. */
    for (const css_rule *rule = sheet->rule_list; rule != NULL; rule = rule->next) {
        if (rule->type == CSS_RULE_IMPORT) {
            if (!sheet_scope(((const css_rule_import *)rule)->sheet, depth + 1, revision))
                return false;
        } else if (rule->type != CSS_RULE_CHARSET && rule->type != CSS_RULE_UNKNOWN) {
            break;
        }
    }
    return true;
}

bool css__sheet_checked_parent_scope(const css_stylesheet *sheet, uint64_t *revision)
{
    return sheet_scope(sheet, 0, revision);
}
