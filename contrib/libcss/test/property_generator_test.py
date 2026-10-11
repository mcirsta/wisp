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
        entries = generator.generate_gperf_input().split("%%")[1].strip().splitlines()
        names = {entry.split(",")[0] for entry in entries}
        self.assertEqual(names, {name.replace("_", "-") for name in self.reader.toml_data})
        for name in self.reader.toml_data:
            self.assertEqual(ids.count(f"    CSS_DECL_{name.upper()},"), 1)
        # A wrapped longhand must not be classified as a shorthand.
        color = next(e for e in entries if e.startswith("border-top-color,"))
        side = next(e for e in entries if e.startswith("border-top,"))
        self.assertIn("false, false, NULL, 0", color)
        self.assertIn("true, false, css_decl_members_border_top, 3", side)
        for name, prop in self.reader.toml_data.items():
            entry = next(e for e in entries if e.startswith(name.replace("_", "-") + ","))
            kind = prop.get("declaration_value", "none").upper()
            self.assertTrue(entry.endswith("CSS_DECL_VALUE_" + kind), name)

    def test_unknown_value_family(self):
        self.reader.toml_data["width"]["declaration_value"] = "typo"
        with self.assertRaisesRegex(ValueError, "width: unknown declaration_value"):
            self.reader.validate_declarations()

    def test_value_family_arity(self):
        for kind in ("pair_length", "pair_keyword"):
            with self.subTest(kind=kind):
                self.reader.toml_data["margin"]["declaration_value"] = kind
                with self.assertRaisesRegex(ValueError, "margin: declaration_value requires 2"):
                    self.reader.validate_declarations()

    def test_ordered_shorthand_members(self):
        for name in ("flex", "flex_flow", "list_style", "columns"):
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
                     "list_style_position", "list_style_image", "column_width", "column_count"):
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
                     "aspect_ratio", "border_spacing", "clip", "font_family", "colour"):
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
