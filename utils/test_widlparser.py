#!/usr/bin/env python3
"""
Integration tests for widlparser with neosurf WebIDL files.

Verifies that widlparser correctly parses all project IDL files.

Run with: python -m pytest utils/test_widlparser.py -v
Or:       python utils/test_widlparser.py
"""

import unittest
import os

try:
    import widlparser
except ImportError:
    raise ImportError("widlparser not installed. Install via: pip install widlparser or pacman -S python-widlparser")


class TestWidlparserIntegration(unittest.TestCase):
    """Test widlparser with neosurf IDL files."""

    @classmethod
    def setUpClass(cls):
        """Set up paths to IDL files."""
        # Find the WebIDL directory relative to this test file
        test_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(test_dir)
        cls.idl_dir = os.path.join(
            project_root, 'src', 'content', 'handlers', 'javascript', 'WebIDL'
        )
        
        if not os.path.exists(cls.idl_dir):
            raise unittest.SkipTest(f"IDL directory not found: {cls.idl_dir}")

    def _parse_idl_file(self, filename):
        """Helper to parse an IDL file and return the parser."""
        path = os.path.join(self.idl_dir, filename)
        parser = widlparser.Parser()
        with open(path, 'r', encoding='utf-8') as f:
            parser.parse(f.read())
        return parser

    def test_parse_console_idl(self):
        """Test parsing console.idl."""
        parser = self._parse_idl_file('console.idl')
        
        # Should have at least Console interface
        construct_names = [c.name for c in parser.constructs]
        self.assertIn('Console', construct_names)
        
        # Console should have methods like log, error, warn
        console = parser.find('Console')
        self.assertIsNotNone(console)
        
        member_names = [m.name for m in console.members if hasattr(m, 'name')]
        self.assertIn('log', member_names)
        self.assertIn('error', member_names)
        self.assertIn('warn', member_names)

    def test_parse_dom_idl(self):
        """Test parsing dom.idl."""
        parser = self._parse_idl_file('dom.idl')
        
        # Should have core DOM interfaces
        construct_names = [c.name for c in parser.constructs]
        self.assertIn('Node', construct_names)
        self.assertIn('Element', construct_names)
        self.assertIn('Document', construct_names)
        self.assertIn('Event', construct_names)
        self.assertIn('EventTarget', construct_names)
        
        # Node should inherit from EventTarget
        node = parser.find('Node')
        self.assertIsNotNone(node)
        if hasattr(node, 'inheritance') and node.inheritance:
            # widlparser includes ': ' prefix, so strip it
            inheritance = str(node.inheritance).strip().lstrip(':').strip()
            self.assertEqual(inheritance, 'EventTarget')

    def test_parse_html_idl(self):
        """Test parsing html.idl."""
        parser = self._parse_idl_file('html.idl')
        
        # Should have HTML element interfaces
        construct_names = [c.name for c in parser.constructs]
        self.assertIn('HTMLElement', construct_names)

    def test_parse_all_idl_files(self):
        """Test that all IDL files parse without errors."""
        idl_files = [f for f in os.listdir(self.idl_dir) if f.endswith('.idl')]
        self.assertGreater(len(idl_files), 0, "No IDL files found")
        
        for filename in idl_files:
            with self.subTest(file=filename):
                parser = self._parse_idl_file(filename)
                # Should parse without exception and have some content
                self.assertGreater(
                    len(parser.constructs), 0,
                    f"No constructs parsed from {filename}"
                )

    def test_interface_members(self):
        """Test that interface members are correctly parsed."""
        parser = self._parse_idl_file('dom.idl')
        
        element = parser.find('Element')
        self.assertIsNotNone(element)
        
        # Element should have getAttribute method
        member_names = [m.name for m in element.members if hasattr(m, 'name')]
        self.assertIn('getAttribute', member_names)
        self.assertIn('setAttribute', member_names)

    def test_node_constants(self):
        """Test that Node constants are parsed."""
        parser = self._parse_idl_file('dom.idl')
        
        node = parser.find('Node')
        self.assertIsNotNone(node)
        
        # Node should have ELEMENT_NODE constant
        constants = [m for m in node.members 
                     if hasattr(m, 'name') and m.name and 
                     m.name.isupper()]
        constant_names = [c.name for c in constants]
        
        # Check for at least one node type constant
        self.assertTrue(
            any('NODE' in name for name in constant_names),
            f"No NODE constants found, got: {constant_names}"
        )


class TestWidlparserAPI(unittest.TestCase):
    """Test widlparser API features we rely on."""

    def test_parser_creation(self):
        """Test that Parser can be created."""
        parser = widlparser.Parser()
        self.assertIsNotNone(parser)

    def test_parse_simple_interface(self):
        """Test parsing a simple interface string."""
        parser = widlparser.Parser()
        parser.parse("""
            interface Foo {
                void bar();
            };
        """)
        
        self.assertEqual(len(parser.constructs), 1)
        self.assertEqual(parser.constructs[0].name, 'Foo')

    def test_find_construct(self):
        """Test finding a construct by name."""
        parser = widlparser.Parser()
        parser.parse("""
            interface Alpha {};
            interface Beta {};
        """)
        
        alpha = parser.find('Alpha')
        self.assertIsNotNone(alpha)
        self.assertEqual(alpha.name, 'Alpha')
        
        beta = parser.find('Beta')
        self.assertIsNotNone(beta)
        self.assertEqual(beta.name, 'Beta')
        
        gamma = parser.find('Gamma')
        self.assertIsNone(gamma)

    def test_interface_inheritance(self):
        """Test parsing interface inheritance."""
        parser = widlparser.Parser()
        parser.parse("""
            interface Parent {};
            interface Child : Parent {};
        """)
        
        child = parser.find('Child')
        self.assertIsNotNone(child)
        if hasattr(child, 'inheritance') and child.inheritance:
            # widlparser includes ': ' prefix, so strip it
            inheritance = str(child.inheritance).strip().lstrip(':').strip()
            self.assertEqual(inheritance, 'Parent')


def main():
    """Run the tests."""
    # Print widlparser info
    print(f"Testing with widlparser")
    if hasattr(widlparser, '__version__'):
        print(f"  Version: {widlparser.__version__}")
    print()
    
    # Run tests
    unittest.main(verbosity=2)


if __name__ == '__main__':
    main()
