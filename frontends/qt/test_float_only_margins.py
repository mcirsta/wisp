#!/usr/bin/env python3
"""Float-only blocks collapse margins without borrowing later siblings' offsets."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def cases():
    # Geometry is (top, border height), relative to the case's content edge.
    # Authored integer sizes, checked against Chromium and CSS 2.2 section 8.3.1.
    for name, options, top, after in (
        ("plain", {}, 13, 17),
        ("listed", dict(tag="li"), 17, 17),
        ("wrapped", dict(wrap=True), 17, 17),
        ("nested", dict(nested=True), 13, 17),
        ("prefix", dict(prefix=10, previous=20), 30, 30),
        ("bottom-larger", dict(bottom=50), 13, 50),
        ("following-larger", dict(after=70), 13, 70),
        ("mixed", dict(bottom=-17), 13, -4),
        ("clear", dict(clear="both"), 13, 61),
        ("clear-large", dict(clear="both", after=70), 13, 70),
        ("nested-margins", dict(nested=True, inner="margin:23px 0 29px"), 29, 29),
        ("nested-after", dict(nested=True, inner="margin:23px 0 29px", after=70), 29, 70),
        ("following-negative", dict(after=-30), 13, -13),
        ("right-float", dict(side="right"), 13, 17),
        ("negative-prefix", dict(prefix=40, top=-13, bottom=-17), 27, 23),
    ):
        expected = dict(empty=(top, 0), floating=(top, 48), after=(after, 8))
        if options.get("wrap"):
            expected["wrapper"] = (top, 0)
        if options.get("nested"):
            expected["inner"] = (top, 0)
        yield name, options, expected
    for name, style, height, float_top, after in (
        ("padded", "padding:5px 0 7px", 12, 18, 42),
        ("minimum", "min-height:20px", 20, 13, 50),
        ("flowroot", "display:flow-root", 48, 13, 78),
    ):
        yield name, dict(style=style), dict(empty=(13, height), floating=(float_top, 48), after=(after, 8))
    yield "no-float", dict(no_float=True), dict(empty=(13, 0), after=(17, 8))
    yield "last", dict(last=True), dict(empty=(13, 0), floating=(13, 48))
    for wrapped in (False, True):
        expected = dict(empty=(13, 56), floating=(13, 48), cleared=(61, 0), after=(86, 8))
        if wrapped:
            expected["wrapper"] = (13, 56)
        yield f"inner-clear-{int(wrapped)}", dict(inner_clear=True, wrap=wrapped), expected
        top = 17 if wrapped else 13
        expected = dict(empty=(top, 0), floating=(top, 48), second=(top + 48, 20), after=(17, 8))
        if wrapped:
            expected["wrapper"] = (top, 0)
        yield f"two-floats-{int(wrapped)}", dict(two=True, wrap=wrapped), expected

    for name, options, expected in (
        ("negative", {}, dict(empty=(-13, 0), floating=(-13, 48), after=(-17, 8))),
        ("negative-right", dict(side="right"), dict(empty=(-13, 0), floating=(-13, 48), after=(-17, 8))),
        ("negative-deep", dict(top=-73), dict(empty=(-73, 0), floating=(-73, 48), after=(-73, 8))),
        ("negative-clear", dict(top=-73, clear="both"),
         dict(empty=(-73, 0), floating=(-73, 48), after=(-25, 8))),
        ("negative-unmatched-block-clear", dict(top=-73, clear="right"),
         dict(empty=(-73, 0), floating=(-73, 48), after=(-73, 8))),
        ("negative-br-clear", dict(top=-73, break_clear="left", style="font:20px/20px sans-serif"),
         dict(empty=(-73, 48), floating=(-73, 48), after=(-42, 8))),
        ("negative-br-unmatched", dict(top=-73, break_clear="right", style="font:20px/20px sans-serif"),
         dict(empty=(-73, 20), floating=(-73, 48), after=(-70, 8))),
        ("negative-two", dict(top=-73, two=True),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-25, 20), after=(-73, 8))),
        ("negative-clear-float", dict(top=-73, two=True, second_width=40, second_clear="left"),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-25, 20), after=(-73, 8))),
        ("negative-unmatched-clear", dict(top=-73, two=True, second_width=40, second_clear="right"),
         dict(empty=(-73, 0), floating=(-73, 48), second=(-73, 20), after=(-73, 8))),
        ("negative-after-float", dict(top=-100, prefix=60, preceding=True),
         dict(prefix=(0, 60), preceding=(60, 16), empty=(-40, 0), floating=(60, 48), after=(-40, 8))),
    ):
        yield name, dict(top=-13, bottom=-17) | options, expected

    # Floats before/between block children must not commit the parent's top
    # before those children's adjoining margins have been collected.
    for name, options, top, child_top, child_height, parent_height, float_top, after in (
        ("before-child", {}, 29, 29, 0, 0, 29, 29),
        ("between-children", dict(float_after_child=True), 29, 29, 0, 0, 29, 29),
        ("before-wrapped", dict(wrap=True), 29, 29, 0, 0, 29, 29),
        ("before-right", dict(side="right"), 29, 29, 0, 0, 29, 29),
        ("before-following", dict(after=70), 29, 29, 0, 0, 29, 70),
        ("before-last", dict(last=True), 29, 29, 0, 0, 29, None),
        ("before-prefix", dict(prefix=60, previous=31), 91, 91, 0, 0, 91, 91),
        ("before-negative", dict(child="margin:-23px 0 -29px"), -16, -16, 0, 0, -16, -12),
        ("before-mixed", dict(child="margin:23px 0 -29px"), -6, -6, 0, 0, -6, -6),
        ("before-clear", dict(child="clear:left"), 13, 61, 0, 54, 13, 84),
        ("before-unmatched-clear", dict(child="clear:right"), 29, 29, 0, 0, 29, 29),
        ("before-height", dict(child="height:10px"), 23, 23, 10, 10, 23, 62),
        ("before-padding", dict(child="padding-top:5px"), 23, 23, 5, 5, 23, 57),
        ("before-minimum", dict(child="min-height:20px"), 23, 23, 20, 20, 23, 72),
        ("before-parent-padding", dict(style="padding-top:5px"), 13, 41, 0, 5, 18, 47),
        ("before-parent-bottom", dict(style="padding-bottom:7px"), 29, 29, 0, 7, 29, 53),
        ("before-parent-minimum", dict(style="min-height:20px"), 29, 29, 0, 20, 29, 66),
        ("before-line", dict(text_after=True, style="font:13px/20px sans-serif"), 29, 29, 0, 20, 29, 66),
        ("child-clear-large", dict(child="clear:left;margin-top:100px"), 13, 61, 0, 48, 13, 78),
        ("child-clear-zero-amount", dict(child="clear:left;margin-top:48px"), 13, 61, 0, 48, 13, 78),
        ("child-clear-large-right", dict(side="right", child="clear:right;margin-top:100px"), 13, 61, 0, 48, 13, 78),
        ("child-clear-large-both", dict(child="clear:both;margin-top:100px"), 13, 61, 0, 48, 13, 78),
        ("child-clear-large-unmatched", dict(child="clear:right;margin-top:100px"), 100, 100, 0, 0, 100, 100),
        ("child-clear-large-nonempty", dict(child="clear:left;margin-top:100px;height:10px"), 13, 61, 10, 58, 13, 100),
        ("child-clear-large-unmatched-height", dict(child="clear:right;margin-top:100px;height:10px"),
         100, 100, 10, 10, 100, 139),
        ("child-clear-large-padded-child", dict(child="clear:left;margin-top:100px;padding-top:5px"),
         13, 61, 5, 53, 13, 95),
        ("child-clear-large-padded-parent", dict(child="clear:left;margin-top:100px", style="padding-top:5px"),
         13, 118, 0, 5, 18, 118),
        ("child-clear-large-bottom-parent", dict(child="clear:left;margin-top:100px", style="padding-bottom:7px"),
         13, 61, 0, 55, 13, 85),
        ("child-clear-large-minimum-parent", dict(child="clear:left;margin-top:100px", style="min-height:20px"),
         13, 61, 0, 48, 13, 78),
        ("child-clear-large-fixed-parent", dict(child="clear:left;margin-top:100px", style="height:40px"),
         13, 61, 0, 40, 13, 70),
        ("child-clear-large-negative-parent", dict(top=-13, child="clear:left;margin-top:100px"),
         -13, 35, 0, 48, -13, 52),
        ("child-clear-large-prefix", dict(prefix=60, previous=31, child="clear:left;margin-top:100px"),
         91, 139, 0, 48, 91, 156),
        ("child-clear-large-wrapped", dict(wrap=True, child="clear:left;margin-top:100px"), 13, 61, 0, 48, 13, 78),
        ("child-clear-large-nested", dict(child_wrapper="margin:31px 0 37px", child="clear:left;margin-top:100px"),
         31, 79, 0, 48, 31, 116),
        ("child-clear-large-empty-sibling", dict(before_child="margin:23px 0 29px", child="clear:left;margin-top:100px"),
         29, 77, 0, 48, 29, 94),
        ("child-clear-large-nonempty-sibling", dict(before_child="height:10px;margin:23px 0 29px", before_height=10,
                                             child="clear:left;margin-top:100px"), 23, 133, 0, 10, 23, 133),
        ("child-clear-large-line-before", dict(text_before=True, style="font:13px/20px sans-serif",
                                        child="clear:left;margin-top:100px"), 13, 133, 0, 20, 13, 133),
        ("child-clear-large-line-after", dict(text_after=True, style="font:13px/20px sans-serif",
                                       child="clear:left;margin-top:100px"), 13, 61, 0, 68, 13, 98),
    ):
        expected = dict(empty=(top, parent_height), inner=(child_top, child_height), floating=(float_top, 48))
        if after is not None:
            expected["after"] = (after, 8)
        if options.get("wrap"):
            expected["wrapper"] = (top, parent_height)
        if options.get("float_after_child"):
            expected["second"] = (float_top, 20)
        if "child_wrapper" in options:
            expected["middle"] = (top, parent_height)
        if "before_child" in options:
            expected["before"] = (top, options.get("before_height", 0))
        yield name, dict(child="") | options, expected

    # A parent's specified height separates its bottom edge, not its first
    # child's top. Check the same chains with and without preceding floats.
    for name, options, top, height, child_top, child_height, float_top, after in (
        ("fixed-empty", {}, 29, 40, 29, 0, 29, 86),
        ("fixed-child-height", dict(child="height:10px"), 23, 40, 23, 10, 23, 80),
        ("fixed-child-padding", dict(child="padding-top:5px"), 23, 40, 23, 5, 23, 80),
        ("fixed-negative", dict(child="margin:-23px 0 -29px"), -16, 40, -16, 0, -16, 41),
        ("fixed-mixed", dict(child="margin:23px 0 -29px"), -6, 40, -6, 0, -6, 51),
        ("fixed-parent-negative", dict(top=-13, bottom=-17), 16, 40, 16, 0, 16, 39),
        ("fixed-following", dict(after=70), 29, 40, 29, 0, 29, 139),
        ("fixed-prefix", dict(prefix=60, previous=31), 91, 40, 91, 0, 91, 148),
        ("fixed-top-padding", dict(style="height:40px;padding-top:5px"), 13, 45, 41, 0, 18, 75),
        ("fixed-top-border", dict(style="height:40px;border-top:3px solid"), 13, 43, 39, 0, 16, 73),
        ("fixed-bottom-padding", dict(style="height:40px;padding-bottom:7px"), 29, 47, 29, 0, 29, 93),
        ("fixed-bottom-border", dict(style="height:40px;border-bottom:3px solid"), 29, 43, 29, 0, 29, 89),
        ("fixed-minimum", dict(style="height:40px;min-height:60px"), 29, 60, 29, 0, 29, 106),
        ("fixed-maximum", dict(style="height:60px;max-height:40px"), 29, 40, 29, 0, 29, 86),
        ("fixed-minmax-conflict", dict(style="height:40px;min-height:60px;max-height:20px"),
         29, 60, 29, 0, 29, 106),
        ("fixed-zero-empty", dict(style="height:0"), 29, 0, 29, 0, 29, 29),
        ("fixed-zero-nonempty", dict(style="height:0", child="height:10px"), 23, 0, 23, 10, 23, 40),
        ("fixed-calc", dict(style="height:calc(20px + 20px)"), 29, 40, 29, 0, 29, 86),
        ("fixed-flowroot", dict(style="height:40px;display:flow-root"), 13, 40, 36, 0, 13, 70),
        ("fixed-child-flowroot", dict(child="display:flow-root"), 23, 40, 23, 0, 23, 80),
        ("fixed-nested", dict(child_wrapper="margin:31px 0 37px"), 37, 40, 37, 0, 37, 94),
        ("fixed-nested-height", dict(child_wrapper="height:20px;margin:31px 0 37px"), 31, 40, 31, 0, 31, 88),
        ("fixed-wrapped", dict(wrap=True), 29, 40, 29, 0, 29, 86),
        ("fixed-unmatched-clear", dict(child="clear:right"), 29, 40, 29, 0, 29, 86),
        ("fixed-text-before", dict(text_before=True), 13, 40, 56, 0, 13, 70),
        ("fixed-text-after", dict(text_after=True), 29, 40, 29, 0, 29, 86),
        ("fixed-before-empty", dict(before_child="margin:31px 0 37px"), 37, 40, 37, 0, 37, 94),
        ("fixed-before-height", dict(before_child="height:10px;margin:31px 0 37px"), 31, 40, 78, 0, 31, 88),
        ("fixed-between-floats", dict(float_after_child=True), 29, 40, 29, 0, 29, 86),
    ):
        for floating in (False, True):
            expected = dict(empty=(top, height), inner=(child_top, child_height), after=(after, 8))
            if floating:
                expected["floating"] = (float_top, 48)
            if options.get("wrap"):
                expected["wrapper"] = (top, height)
            if "child_wrapper" in options:
                expected["middle"] = (top, 20 if name == "fixed-nested-height" else 0)
            if "before_child" in options:
                expected["before"] = (top, 10 if name == "fixed-before-height" else 0)
            if options.get("float_after_child"):
                expected["second"] = (top, 20)
            case = dict(style="height:40px", child="", no_float=not floating) | options
            case["style"] += ";font:13px/20px sans-serif"
            yield name + ("-float" if floating else "-plain"), case, expected


    # Deferred clearance with measured zero-height floats: Firefox reference.
    # Chromium introduces clearance at this coincident edge; these cases use
    # the same no-clearance/equality rule as our existing clearance tests.
    for name, options, expected in (
        ("plain", {},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("small", {"child": "clear:left;margin-top:23px"},
         {"empty": (29, 0), "floating": (29, 0), "inner": (29, 0), "after": (29, 8)}),
        ("zero", {"child": "clear:left;margin-top:0"},
         {"empty": (29, 0), "floating": (29, 0), "inner": (29, 0), "after": (29, 8)}),
        ("negative", {"child": "clear:left;margin-top:-23px"},
         {"empty": (6, 0), "floating": (6, 0), "inner": (6, 0), "after": (6, 8)}),
        ("nonempty", {"child": "clear:left;margin-top:100px;height:10px"},
         {"empty": (100, 10), "floating": (100, 0), "inner": (100, 10), "after": (139, 8)}),
        ("padded-child", {"child": "clear:left;margin-top:100px;padding-top:5px"},
         {"empty": (100, 5), "floating": (100, 0), "inner": (100, 5), "after": (134, 8)}),
        ("unmatched", {"child": "clear:right;margin-top:100px"},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("right", {"side": "right", "child": "clear:right;margin-top:100px", "float_x": 260},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("both", {"child": "clear:both;margin-top:100px"},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("parent-pad", {"style": "padding-top:5px"},
         {"empty": (13, 5), "floating": (18, 0), "inner": (118, 0), "after": (118, 8)}),
        ("parent-border", {"style": "border-top:3px solid"},
         {"empty": (13, 3), "floating": (16, 0), "inner": (116, 0), "after": (116, 8)}),
        ("parent-flowroot", {"style": "display:flow-root"},
         {"empty": (13, 100), "floating": (13, 0), "inner": (113, 0), "after": (130, 8)}),
        ("parent-min", {"style": "min-height:20px"},
         {"empty": (100, 20), "floating": (100, 0), "inner": (100, 0), "after": (137, 8)}),
        ("parent-fixed", {"style": "height:40px"},
         {"empty": (100, 40), "floating": (100, 0), "inner": (100, 0), "after": (157, 8)}),
        ("nested", {"child_wrapper": ""},
         {"empty": (100, 0), "floating": (100, 0), "middle": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("wrap", {"wrap": True},
         {"wrapper": (100, 0), "empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("before-empty", {"before_child": "margin:23px 0 29px"},
         {"empty": (100, 0), "floating": (100, 0), "before": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("before-height", {"before_child": "height:10px;margin:23px 0 29px"},
         {"empty": (23, 10), "floating": (23, 0), "before": (23, 10), "inner": (133, 0), "after": (133, 8)}),
        ("nested-margins", {"child_wrapper": "margin:31px 0 37px"},
         {"empty": (100, 0), "floating": (100, 0), "middle": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("after-text", {"text_after": True},
         {"empty": (100, 20), "floating": (100, 0), "inner": (100, 0), "after": (137, 8)}),
        ("before-text", {"text_before": True},
         {"empty": (13, 20), "floating": (13, 0), "inner": (133, 0), "after": (133, 8)}),
        ("before-prefix", {"prefix": 60, "previous": 31},
         {"prefix": (0, 60), "empty": (160, 0), "floating": (160, 0), "inner": (160, 0), "after": (160, 8)}),
        ("negative-parent", {"top": -13},
         {"empty": (87, 0), "floating": (87, 0), "inner": (87, 0), "after": (87, 8)}),
        ("negative-margin", {"top": -13, "child": "clear:left;margin-top:-100px"},
         {"empty": (-71, 0), "floating": (-71, 0), "inner": (-71, 0), "after": (-71, 8)}),
        ("bottom-large", {"child": "clear:left;margin:100px 0 170px"},
         {"empty": (170, 0), "floating": (170, 0), "inner": (170, 0), "after": (170, 8)}),
        ("after-large", {"after": 200},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (200, 8)}),
        ("parent-large", {"top": 150},
         {"empty": (150, 0), "floating": (150, 0), "inner": (150, 0), "after": (150, 8)}),
        ("negative-both", {"top": -13, "child": "clear:left;margin:-100px 0 -29px"},
         {"empty": (-100, 0), "floating": (-100, 0), "inner": (-100, 0), "after": (-83, 8)}),
        ("two-clears", {"later_child": "clear:left;margin:200px 0 39px"},
         {"empty": (200, 0), "floating": (200, 0), "inner": (200, 0), "later": (200, 0), "after": (200, 8)}),
        ("two-clears-small", {"later_child": "clear:left;margin:11px 0 39px"},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "later": (100, 0), "after": (100, 8)}),
        ("two-floats", {"float_after_child": True, "later_height": 0, "later_child": "clear:left;margin:200px 0 39px"},
         {"empty": (200, 0), "floating": (200, 0), "inner": (200, 0),
          "second": (200, 0), "later": (200, 0), "after": (200, 8)}),
        ("float-margin", {"float_style": "margin-top:5px"},
         {"empty": (13, 5), "floating": (18, 0), "inner": (18, 0), "after": (35, 8)}),
        ("float-negative", {"float_style": "margin-bottom:-20px"},
         {"empty": (100, 0), "floating": (100, 0), "inner": (100, 0), "after": (100, 8)}),
        ("parent-max", {"style": "height:40px;max-height:20px"},
         {"empty": (100, 20), "floating": (100, 0), "inner": (100, 0), "after": (137, 8)}),
        ("external-pass", {"external": "left", "child": "clear:left;margin-top:200px"},
         {"external": (0, 160), "empty": (200, 0), "floating": (200, 0), "inner": (200, 0), "after": (200, 8)}),
        ("external-right-pass", {"external": "right", "side": "right",
                                 "child": "clear:right;margin-top:200px", "float_x": 260},
         {"external": (0, 160), "empty": (200, 0), "floating": (200, 0), "inner": (200, 0), "after": (200, 8)}),
    ):
        options = dict(float_height=0, float_x=0, child="clear:left;margin-top:100px") | options
        options["style"] = "font:13px/20px sans-serif;" + options.get("style", "")
        yield "deferred-" + name, options, expected


def fixture_html():
    parts = ['<!doctype html><meta charset="utf-8"><title>Float-only margins</title><style>',
             'body{margin:0}.case{display:flow-root;width:300px;border:1px solid;margin:10px}',
             'ol{margin:0;padding:0}li{list-style:none}</style>']
    for name, c, _ in cases():
        def element(role, inside="", style="", tag="div"):
            return f'<{tag} id="{name}-{role}" style="{style}">{inside}</{tag}>'

        floating = "" if c.get("no_float") else element(
            "floating", style=f'float:{c.get("side", "left")};width:40px;'
                              f'height:{c.get("float_height", 48)}px;background:#c66;{c.get("float_style", "")}')
        if c.get("two"):
            floating += element("second", style=f'float:left;width:{c.get("second_width", 280)}px;'
                                f'clear:{c.get("second_clear", "none")};height:20px;background:#66c')
        if c.get("inner_clear"):
            floating += element("cleared", style="clear:both;margin:11px 0 19px")
        if c.get("break_clear"):
            floating += f'<br style="clear:{c["break_clear"]}">'
        inside = element("inner", floating, c.get("inner", "")) if c.get("nested") else floating
        if c.get("text_before"):
            inside = "text" + inside
        if "before_child" in c:
            inside += element("before", style=c["before_child"])
        if "child" in c:
            child = element("inner", style="margin:23px 0 29px;" + c["child"])
            if "child_wrapper" in c:
                child = element("middle", child, c["child_wrapper"])
            inside += child
        if c.get("float_after_child"):
            inside += element("second", style=f'float:left;width:40px;'
                                              f'height:{c.get("later_height", 20)}px;background:#66c')
        if c.get("later_child"):
            inside += element("later", style=c["later_child"])
        if c.get("text_after"):
            inside += "text"
        empty = element("empty", inside,
                        f'margin:{c.get("top", 13)}px 0 {c.get("bottom", 17)}px;{c.get("style", "")}',
                        c.get("tag", "div"))
        if c.get("wrap"):
            empty = element("wrapper", empty)
        if c.get("tag") == "li":
            empty = "<ol>" + empty + "</ol>"
        after = "" if c.get("last") else element(
            "after", style=f'height:8px;margin-top:{c.get("after", 0)}px;clear:{c.get("clear", "none")};background:#6a6')
        prefix = element("prefix", style=f'height:{c["prefix"]}px;margin-bottom:{c.get("previous", 0)}px') \
            if "prefix" in c else ""
        if c.get("external"):
            prefix += element("external", style=f'float:{c["external"]};width:40px;height:160px')
        if c.get("preceding"):
            prefix += element("preceding", style="float:left;width:40px;height:16px;background:#cc6")
        parts.append(f'<section class="case" id="{name}-case">{prefix}{empty}{after}</section>')
    return "\n".join(parts)


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1",
               QT_SCREEN_SCALE_FACTORS="1", QT_FONT_DPI="96")
    with tempfile.TemporaryDirectory(prefix="wisp-float-margins-") as tmp:
        fixture = Path(tmp) / "float-margins.html"
        fixture.write_text(fixture_html())
        dest = Path(tmp) / "capture"
        args = [str(binary), "--capture-dir", str(dest), "--capture-size", "390x800",
                "--capture-settle", "100", "--capture-timeout", "15000",
                "--capture-resize", "500x900", "--capture-resize", "390x800", "--enable_javascript=0"]
        for name, _, expected in cases():
            for suffix in ("case", *expected):
                args += ["--capture-id", name + "-" + suffix]
        process = subprocess.run([*args, str(fixture)], env=env, capture_output=True, text=True, timeout=35)
        assert process.returncode == 0, process.stderr
        assert "runtime error:" not in process.stderr, process.stderr
        meta = json.loads((dest / "capture.json").read_text())
        assert meta["result"] == "complete" and meta["load_complete"], meta
        steps = json.loads((dest / "steps.json").read_text())
        assert len(steps) == 3, len(steps)
        for index, report in enumerate(steps):
            assert all(e["status"] == "ok" and e.get("boxes") for e in report["elements"]), report["elements"]
            boxes = {e["id"]: e["boxes"][0]["border_box"] for e in report["elements"]}
            for name, options, expected in cases():
                case = boxes[name + "-case"]
                for suffix, geometry in expected.items():
                    box = boxes[name + "-" + suffix]
                    actual = box["y"] - case["y"] - 1, box["height"]
                    assert actual == geometry, (index, name, suffix, actual, geometry)
                if options.get("float_x") is not None:
                    actual_x = boxes[name + "-floating"]["x"] - case["x"] - 1
                    assert actual_x == options["float_x"], (index, name, actual_x, options["float_x"])
                # A negative bottom margin can put the final flow edge above
                # an earlier block's bottom; that visible overflow adds no height.
                flow_top, flow_height = expected.get("after", expected["empty"])
                float_ends = [expected[key][0] + expected[key][1]
                              for key in ("floating", "second", "preceding", "external") if key in expected]
                height = max(0, flow_top + flow_height, *float_ends) + 2
                assert case["height"] == height, (index, name, case, height)
        assert steps[0]["elements"] == steps[2]["elements"], "Resize round trip changed geometry"
    print(f"Float-only margins: {len(list(cases()))} cases passed before and after resizing.")


if __name__ == "__main__":
    main()
