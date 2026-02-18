/* This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2012 Daniel Silverstone <dsilvers@netsurf-browser.org>
 */

#ifndef SVGTINY_STRING_ACTION2
#error No action defined
#endif

#define SVGTINY_STRING_ACTION(s) SVGTINY_STRING_ACTION2(s, s)

SVGTINY_STRING_ACTION(svg)
SVGTINY_STRING_ACTION(viewBox)
SVGTINY_STRING_ACTION(preserveAspectRatio)
SVGTINY_STRING_ACTION(a)
SVGTINY_STRING_ACTION(d)
SVGTINY_STRING_ACTION(g)
SVGTINY_STRING_ACTION(r)
SVGTINY_STRING_ACTION(x)
SVGTINY_STRING_ACTION(y)
SVGTINY_STRING_ACTION(cx)
SVGTINY_STRING_ACTION(cy)
SVGTINY_STRING_ACTION(rx)
SVGTINY_STRING_ACTION(ry)
SVGTINY_STRING_ACTION(x1)
SVGTINY_STRING_ACTION(y1)
SVGTINY_STRING_ACTION(x2)
SVGTINY_STRING_ACTION(y2)
SVGTINY_STRING_ACTION(path)
SVGTINY_STRING_ACTION(points)
SVGTINY_STRING_ACTION(width)
SVGTINY_STRING_ACTION(height)
SVGTINY_STRING_ACTION(rect)
SVGTINY_STRING_ACTION(circle)
SVGTINY_STRING_ACTION(ellipse)
SVGTINY_STRING_ACTION(line)
SVGTINY_STRING_ACTION(polyline)
SVGTINY_STRING_ACTION(polygon)
SVGTINY_STRING_ACTION(text)
SVGTINY_STRING_ACTION(tspan)
SVGTINY_STRING_ACTION(fill)
SVGTINY_STRING_ACTION(stroke)
SVGTINY_STRING_ACTION(style)
SVGTINY_STRING_ACTION(transform)
SVGTINY_STRING_ACTION(linearGradient)
SVGTINY_STRING_ACTION(radialGradient)
SVGTINY_STRING_ACTION(href)
SVGTINY_STRING_ACTION(stop)
SVGTINY_STRING_ACTION(offset)
SVGTINY_STRING_ACTION(gradientUnits)
SVGTINY_STRING_ACTION(gradientTransform)
SVGTINY_STRING_ACTION(userSpaceOnUse)
SVGTINY_STRING_ACTION(use)
/* Hyphenated attribute names - use SVGTINY_STRING_ACTION3 with quoted strings
 */
#ifdef SVGTINY_STRING_ACTION3
SVGTINY_STRING_ACTION3(stroke_width, "stroke-width")
SVGTINY_STRING_ACTION3(stop_color, "stop-color")
SVGTINY_STRING_ACTION3(fill_opacity, "fill-opacity")
SVGTINY_STRING_ACTION3(stroke_opacity, "stroke-opacity")
SVGTINY_STRING_ACTION3(fill_rule, "fill-rule")
SVGTINY_STRING_ACTION3(stroke_linecap, "stroke-linecap")
SVGTINY_STRING_ACTION3(stroke_linejoin, "stroke-linejoin")
SVGTINY_STRING_ACTION3(stroke_miterlimit, "stroke-miterlimit")
SVGTINY_STRING_ACTION3(stroke_dasharray, "stroke-dasharray")
SVGTINY_STRING_ACTION3(stroke_dashoffset, "stroke-dashoffset")
SVGTINY_STRING_ACTION3(font_size, "font-size")
SVGTINY_STRING_ACTION3(text_anchor, "text-anchor")
SVGTINY_STRING_ACTION3(font_weight, "font-weight")
#else
/* Fallback for contexts that don't define SVGTINY_STRING_ACTION3 */
SVGTINY_STRING_ACTION2(stroke_width, stroke - width)
SVGTINY_STRING_ACTION2(stop_color, stop - color)
SVGTINY_STRING_ACTION2(fill_opacity, fill - opacity)
SVGTINY_STRING_ACTION2(stroke_opacity, stroke - opacity)
SVGTINY_STRING_ACTION2(fill_rule, fill - rule)
SVGTINY_STRING_ACTION2(stroke_linecap, stroke - linecap)
SVGTINY_STRING_ACTION2(stroke_linejoin, stroke - linejoin)
SVGTINY_STRING_ACTION2(stroke_miterlimit, stroke - miterlimit)
SVGTINY_STRING_ACTION2(stroke_dasharray, stroke - dasharray)
SVGTINY_STRING_ACTION2(stroke_dashoffset, stroke - dashoffset)
SVGTINY_STRING_ACTION2(font_size, font - size)
SVGTINY_STRING_ACTION2(text_anchor, text - anchor)
SVGTINY_STRING_ACTION2(font_weight, font - weight)
#endif
SVGTINY_STRING_ACTION2(zero_percent, 0 %)
SVGTINY_STRING_ACTION2(hundred_percent, 100 %)
SVGTINY_STRING_ACTION(dx)
SVGTINY_STRING_ACTION(dy)

#undef SVGTINY_STRING_ACTION
