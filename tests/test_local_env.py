"""Private configuration is data, never executable shell input."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from local_env import env_int, env_path, env_value, parse_env, read_env


class LocalEnvironmentTests(unittest.TestCase):
    def test_literal_windows_paths_quotes_comments_and_shell_text(self):
        values = parse_env("\ufeff# private\nexport PATH_VALUE='C:\\Music Rig\\tool.exe' # note\n"
                           'TOKEN="$(do-not-run) ${OTHER} `literal` # fragment"\n'
                           'RAW=abc#123 # comment\nEMPTY=\n')
        self.assertEqual(values['PATH_VALUE'], r'C:\Music Rig\tool.exe')
        self.assertEqual(values['TOKEN'], '$(do-not-run) ${OTHER} `literal` # fragment')
        self.assertEqual(values['RAW'], 'abc#123')
        self.assertEqual(values['EMPTY'], '')

    def test_invalid_input_never_echoes_its_value(self):
        for text in ('BAD KEY=private-value', 'KEY="private-value',
                     'KEY=private-value\nKEY=again', 'KEY="private-value" extra'):
            with self.subTest(text=text), self.assertRaises(ValueError) as error:
                parse_env(text)
            self.assertNotIn('private-value', str(error.exception))

    def test_process_environment_overrides_file_without_exporting_it(self):
        with patch.dict(os.environ, {'THY666_TEST_VALUE': 'process'}, clear=True), \
                patch('local_env.read_env', return_value={'THY666_TEST_VALUE': 'file', 'SECRET': 'private'}):
            self.assertEqual(env_value('THY666_TEST_VALUE', 'default'), 'process')
            self.assertEqual(env_value('SECRET'), 'private')
            self.assertNotIn('SECRET', os.environ)

    def test_missing_and_empty_values_use_default(self):
        with patch.dict(os.environ, {}, clear=True), \
                patch('local_env.read_env', return_value={'THY666_EMPTY': ''}):
            self.assertEqual(env_value('THY666_EMPTY', 'fallback'), 'fallback')
            self.assertIsNone(env_value('THY666_MISSING'))
            self.assertIsNone(env_path('THY666_MISSING'))

    def test_relative_paths_are_workspace_relative(self):
        with patch.dict(os.environ, {'THY666_TEST_PATH': '.tools/inputs/test.json'}, clear=True):
            self.assertEqual(env_path('THY666_TEST_PATH'), (ROOT / '.tools/inputs/test.json').resolve())

    def test_empty_process_value_can_fall_back_to_file(self):
        with patch.dict(os.environ, {'THY666_TEST_VALUE': ''}, clear=True), \
                patch('local_env.read_env', return_value={'THY666_TEST_VALUE': 'file'}):
            self.assertEqual(env_value('THY666_TEST_VALUE'), 'file')

    def test_numeric_bounds_reject_bad_values_without_echo(self):
        for value in ('-1', '65536', 'private-value', '2.5'):
            with patch.dict(os.environ, {'THY666_TEST_PORT': value}, clear=True), self.assertRaises(ValueError):
                env_int('THY666_TEST_PORT', 3330, 1, 65535)
        with patch.dict(os.environ, {'THY666_TEST_PORT': '3330'}, clear=True):
            self.assertEqual(env_int('THY666_TEST_PORT', 1, 1, 65535), 3330)

    def test_missing_file_is_optional(self):
        self.assertEqual(read_env(ROOT / 'build/nonexistent-local-env-test'), {})


if __name__ == '__main__':
    unittest.main()
