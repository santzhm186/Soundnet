#!/usr/bin/env python3
"""Discovery and collision checks for independent node directories."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

path=Path(__file__).resolve().parents[1]/'scripts/generate-node-registry.py'
spec=importlib.util.spec_from_file_location('generator',path)
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

class DiscoveryTests(unittest.TestCase):
    def test_new_module_and_deterministic_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for folder,key in [('second','example.second'),('first','example.first')]:
                node=root/'nodes'/folder/'Node.hpp';node.parent.mkdir(parents=True)
                node.write_text('// soundnet-module: '+key+'\n')
            text,count=generator.generate(root)
            self.assertEqual(count,2)
            self.assertLess(text.index('first/Node.hpp'),text.index('second/Node.hpp'))
            self.assertIn('nodes::first::module()',text)
    def test_optional_editor_discovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            node=root/'nodes'/'example'/'Node.hpp';node.parent.mkdir(parents=True)
            node.write_text('// soundnet-module: example.widget\n')
            (node.parent/'Editor.hpp').write_text('// optional UI module\n')
            text=generator.generate_editors(root)
            self.assertIn('#include "example/Editor.hpp"',text)
            self.assertIn('nodes::example::editor',text)
            self.assertIn('"example.widget"',text)

    def test_duplicate_identity_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for folder in ['first','second']:
                node=root/'nodes'/folder/'Node.hpp';node.parent.mkdir(parents=True)
                node.write_text('// soundnet-module: example.duplicate\n')
            with self.assertRaises(ValueError):generator.generate(root)
    def test_namespace_collision_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for folder,key in [('a-b','example.first'),('a_b','example.second')]:
                node=root/'nodes'/folder/'Node.hpp';node.parent.mkdir(parents=True)
                node.write_text('// soundnet-module: '+key+'\n')
            with self.assertRaises(ValueError):generator.generate(root)

if __name__=='__main__':unittest.main()
