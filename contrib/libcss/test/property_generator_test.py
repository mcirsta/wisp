"""Keep generated declaration identities and shorthand membership coherent."""
import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("property_generator", ROOT / "build-aux/property_generator.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class DeclarationMetadataTest(unittest.TestCase):
    def setUp(self):
        self.reader = GEN.TomlPropertyReader(ROOT / "properties.toml")
        self.reader.parse()

    def test_generated_identity_coverage(self):
        generator = GEN.MultiFileGenerator(self.reader.get_dispatch_order(), self.reader.prop_map)
        generator.toml_reader = self.reader
        ids = generator.generate_declaration_ids()
        output = generator.generate_gperf_input()
        keys = dict(line.split(", ", 1) for line in output.split("%%")[1].strip().splitlines())
        aliases = self.reader.get_legacy_aliases()
        self.assertEqual(set(keys), {name.replace("_", "-") for name in self.reader.toml_data} | set(aliases))
        entries = output.split("static const struct css_prop_entry css_prop_registry[] = {\n")[1]
        entries = entries.split("\n};")[0].splitlines()
        self.assertEqual(len(entries), len(self.reader.toml_data))
        for name in self.reader.toml_data:
            self.assertEqual(ids.count(f"    CSS_DECL_{name.upper()},"), 1)
        for alias, canonical in aliases.items():
            self.assertEqual(keys[alias], keys[canonical])
            self.assertNotIn("CSS_DECL_" + alias.replace("-", "_").upper() + ",", ids)
        # A wrapped longhand must not be classified as a shorthand.
        color = next(e for e in entries if e.startswith('    {"border-top-color",'))
        side = next(e for e in entries if e.startswith('    {"border-top",'))
        self.assertIn("false, false, NULL, 0", color)
        self.assertIn("true, false, css_decl_members_border_top, 3", side)
        for name, prop in self.reader.toml_data.items():
            entry = next(e for e in entries if e.startswith('    {"' + name.replace("_", "-") + '",'))
            kind = prop.get("declaration_value", "none").upper()
            self.assertTrue(entry.endswith("CSS_DECL_VALUE_" + kind + "},"), name)

    def test_background_axes_are_native_longhands(self):
        dispatch = {prop['name'] for prop in self.reader.get_dispatch_order()}
        for kind in ('repeat', 'position'):
            name = 'background_' + kind
            with self.subTest(kind=kind):
                self.assertEqual(self.reader.toml_data[name]['declaration_longhands'],
                                 [name + '_x', name + '_y'])
                self.assertNotIn(name, dispatch)
                for axis in ('x', 'y'):
                    self.assertIn(name + '_' + axis, dispatch)
                    self.assertIn(name + '_' + axis,
                                  self.reader.toml_data['background']['declaration_longhands'])
                self.reader.toml_data[name]['declaration_longhands'].reverse()
                with self.assertRaisesRegex(ValueError, name + ': declaration_value requires ordered members'):
                    self.reader.validate_declarations()
                self.reader.toml_data[name]['declaration_longhands'].reverse()

    def test_white_space_longhands(self):
        self.assertEqual(self.reader.toml_data['white_space']['declaration_longhands'],
                         ['white_space_collapse', 'text_wrap_mode'])
        dispatch = {prop['name']: prop for prop in self.reader.get_dispatch_order()}
        self.assertNotIn('white_space', dispatch)
        for name in ('white_space_collapse', 'text_wrap_mode'):
            self.assertTrue(dispatch[name]['inherited'])
        self.reader.toml_data['white_space']['declaration_longhands'].reverse()
        with self.assertRaisesRegex(ValueError, 'white_space: declaration_value requires ordered members'):
            self.reader.validate_declarations()

    def test_legacy_alias_validation(self):
        for aliases in ("grid-gap", [1], ["GRID-GAP"], ["grid_gap"], ["--gap"], ["gríd-gap"],
                        ["gap"], ["row-gap"], ["grid-row-gap"], ["grid-gap", "grid-gap"]):
            with self.subTest(aliases=aliases):
                self.reader.toml_data["gap"]["legacy_aliases"] = aliases
                with self.assertRaisesRegex(ValueError, "legacy.alias"):
                    self.reader.validate_declarations()

    def test_block_shorthand_catalogue(self):
        generator = GEN.MultiFileGenerator(self.reader.get_dispatch_order(), self.reader.prop_map)
        generator.toml_reader = self.reader
        output = generator.generate_gperf_input()
        entries = output.split("static const struct css_prop_entry css_prop_registry[] = {\n")[1]
        names = [line.split('"')[1] for line in entries.split("\n};")[0].splitlines()]
        catalogue = output.split("static const struct css_prop_entry *const css_shorthand_registry[] = {\n")[1]
        actual = [names[int(line.split('[')[1].split(']')[0])]
                  for line in catalogue.split("\n};")[0].splitlines()]
        expected = sorted((name for name, prop in self.reader.toml_data.items() if prop.get('declaration_longhands')),
                          key=lambda name: (-len(self.reader.toml_data[name]['declaration_longhands']), name))
        self.assertEqual(actual, [name.replace('_', '-') for name in expected])
        self.assertEqual(actual[0], 'border')
        self.assertNotIn('grid-gap', actual)
        maximum = max(len(prop.get('declaration_longhands', [])) for prop in self.reader.toml_data.values())
        self.assertIn(f'CSS_DECL_MAX_LONGHANDS = {maximum}', generator.generate_declaration_ids())

    def test_alias_does_not_add_dispatch_or_parser(self):
        self.reader.toml_data["gap"]["legacy_aliases"].append("extra-legacy-gap")
        self.reader.validate_declarations()
        aliases = self.reader.get_legacy_aliases()
        self.assertEqual(aliases["extra-legacy-gap"], "gap")
        self.assertNotIn("inline-size", aliases)
        self.assertNotIn("block-size", aliases)
        self.assertNotIn("extra_legacy_gap", self.reader.prop_map)
        self.assertNotIn("extra_legacy_gap:", self.reader.generate_properties_gen())
        self.assertFalse(any(prop["name"] == "extra_legacy_gap" for prop in self.reader.get_dispatch_order()))

    def test_unknown_value_family(self):
        self.reader.toml_data["width"]["declaration_value"] = "typo"
        with self.assertRaisesRegex(ValueError, "width: unknown declaration_value"):
            self.reader.validate_declarations()

    def test_aggregate_identity_is_independent_of_rendering_slot(self):
        generator = GEN.MultiFileGenerator(self.reader.get_dispatch_order(), self.reader.prop_map)
        generator.toml_reader = self.reader
        output = generator.generate_gperf_input()
        dispatch = {prop['name'] for prop in self.reader.get_dispatch_order()}
        for aggregate, member in (('font_variant', 'font_variant_caps'),
                                  ('text_decoration', 'text_decoration_line')):
            with self.subTest(aggregate=aggregate):
                self.assertIn(aggregate, dispatch)
                self.assertNotIn(member, dispatch)
                self.assertEqual(self.reader.toml_data[aggregate]['declaration_longhands'], [member])
                self.assertEqual(self.reader.toml_data[aggregate]['enum'], self.reader.toml_data[member]['enum'])
                entry = next(line for line in output.splitlines()
                             if line.startswith('    {"' + aggregate.replace('_', '-') + '",'))
                self.assertIn(f'true, false, css_decl_members_{aggregate}, 1, CSS_DECL_VALUE_SINGLE', entry)
                entry = next(line for line in output.splitlines()
                             if line.startswith('    {"' + member.replace('_', '-') + '",'))
                self.assertIn('false, false, NULL, 0', entry)
                self.assertNotIn(member.replace('_', '-'), self.reader.get_legacy_aliases())

    def test_single_aggregate_requires_one_longhand(self):
        self.reader.toml_data['font_variant']['declaration_longhands'].append('font_weight')
        with self.assertRaisesRegex(ValueError, 'font_variant: declaration_value requires 1'):
            self.reader.validate_declarations()

    def test_value_family_arity(self):
        for kind in ("pair_length", "pair_keyword", "pair_time", "pair_uri"):
            with self.subTest(kind=kind):
                self.reader.toml_data["margin"]["declaration_value"] = kind
                with self.assertRaisesRegex(ValueError, "margin: declaration_value requires 2"):
                    self.reader.validate_declarations()

    def test_ordered_shorthand_members(self):
        for name in ("flex", "flex_flow", "list_style", "columns", "border", "border_top", "outline", "column_rule",
                     "grid_row", "grid_column", "grid_area", "background", "font"):
            with self.subTest(name=name):
                members = self.reader.toml_data[name]["declaration_longhands"]
                members.reverse()
                with self.assertRaisesRegex(ValueError, "requires ordered members"):
                    self.reader.validate_declarations()
                members.reverse()

    def test_shorthand_value_helpers(self):
        spec = self.reader.generate_properties_gen()
        generator = GEN.MultiFileGenerator(self.reader.get_dispatch_order(), self.reader.prop_map)
        generator.toml_reader = self.reader
        declarations = generator.generate_parse_declarations()
        for name in ("flex_grow", "flex_shrink", "flex_basis", "flex_direction", "flex_wrap",
                     "list_style_position", "list_style_image", "column_width", "column_count",
                     "align_content", "justify_content", "font_size", "line_height"):
            self.assertIn(f"css__parse_{name}_value(", declarations)
            helper = next(line for line in spec.splitlines() if line.startswith(name + "_value:"))
            for keyword in ("INHERIT:", "INITIAL:", "REVERT:", "UNSET:"):
                self.assertNotIn(keyword, helper)

    def test_longhand_value_family(self):
        for kind in ("quad_length", "quad_keyword", "quad_colour"):
            with self.subTest(kind=kind):
                self.reader.toml_data["width"]["declaration_value"] = kind
                with self.assertRaisesRegex(ValueError, "width: declaration_value requires 4"):
                    self.reader.validate_declarations()

    def test_scalar_family_cannot_describe_shorthand(self):
        for kind in ("keyword", "uri", "image_list", "keyword_list", "content", "string_list", "counter_list",
                     "aspect_ratio", "border_spacing", "clip", "font_family", "colour", "grid_line",
                     "cursor", "position", "position_axis_list", "text_decoration", "transform",
                     "grid_tracks", "grid_flow", "time", "frequency", "angle", "play_during", "voice_family"):
            with self.subTest(kind=kind):
                self.reader.toml_data["overflow"]["declaration_value"] = kind
                with self.assertRaisesRegex(ValueError, "overflow: declaration_value requires 0"):
                    self.reader.validate_declarations()

    def test_missing_shorthand_members(self):
        del self.reader.toml_data["margin"]["declaration_longhands"]
        with self.assertRaisesRegex(ValueError, "margin: declaration_longhands"):
            self.reader.validate_declarations()

    def test_longhand_cannot_have_members(self):
        self.reader.toml_data["width"]["declaration_longhands"] = ["height"]
        with self.assertRaisesRegex(ValueError, "width: declaration_longhands"):
            self.reader.validate_declarations()

    def test_duplicate_member(self):
        self.reader.toml_data["margin"]["declaration_longhands"] = ["margin_top", "margin_top"]
        with self.assertRaisesRegex(ValueError, "margin: duplicate"):
            self.reader.validate_declarations()

    def test_unknown_member(self):
        self.reader.toml_data["margin"]["declaration_longhands"] = ["margin_typo"]
        with self.assertRaisesRegex(ValueError, "margin: unknown longhand"):
            self.reader.validate_declarations()

    def test_nested_shorthand(self):
        self.reader.toml_data["border"]["declaration_longhands"] = ["border_top"]
        with self.assertRaisesRegex(ValueError, "border: nested shorthand"):
            self.reader.validate_declarations()

    def test_logical_pair_order(self):
        self.reader.toml_data["margin_inline"]["declaration_longhands"].reverse()
        with self.assertRaisesRegex(ValueError, "margin_inline: logical shorthand"):
            self.reader.validate_declarations()

    def test_logical_pair_cannot_be_physical(self):
        self.reader.toml_data["margin_inline"]["declaration_longhands"] = ["margin_left", "margin_right"]
        with self.assertRaisesRegex(ValueError, "margin_inline: logical shorthand"):
            self.reader.validate_declarations()


if __name__ == "__main__":
    unittest.main()
