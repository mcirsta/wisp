/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_bytecode_opcodes_h_
#define css_bytecode_opcodes_h_

#include <inttypes.h>

#define VALUE_IS_CALC 0x007f
#define VALUE_IS_VAR  0x007e

/* Auto-generated opcode enums from properties.gen (property_generator.py) */
#include "opcodes.inc"

/* Manual opcode enums for MANUAL/OPCODES:MANUAL/shared properties */
#include "opcodes_manual.h"

#endif
