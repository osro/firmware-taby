"""Check failure cleanup without opening a port or needing serial packages."""
import ast
from pathlib import Path
import unittest

TOOL = Path(__file__).resolve().parents[1]/'tools/c6_decor_verify.py'


def restoration_function():
    # Execute the actual helper in isolation: all I/O goes through its argument.
    source = ast.parse(TOOL.read_text())
    node = next((n for n in source.body if isinstance(n, ast.FunctionDef)
                 and n.name == 'restore_device'), None)
    if node is None:
        raise AssertionError('Restoration helper missing')
    scope = {}
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(TOOL), 'exec'), scope)
    return scope['restore_device']


class Connection:
    def __init__(self, failures=()):
        self.failures = failures
        self.calls = []

    def query(self, command, prefix):
        self.calls.append(command)
        if command in self.failures:
            raise RuntimeError('Simulated command failure')
        return 'OK'


class DecorVerifierCleanupTests(unittest.TestCase):
    def test_attempts_every_restore_after_multiple_failures_and_reports_failure(self):
        s = Connection(('CLEAR', 'DISPLAY_ORIENTATION 90'))
        with self.assertRaises(RuntimeError):
            restoration_function()(s, '90', 'ble')
        self.assertEqual(s.calls, ['CLEAR', 'DISPLAY_ORIENTATION 90', 'TRANSPORT_MODE ble'])

    def test_success_restores_each_value_and_unknown_values_are_not_invented(self):
        restore = restoration_function()
        s = Connection()
        restore(s, 'left', 'wifi')
        self.assertEqual(s.calls, ['CLEAR', 'DISPLAY_ORIENTATION left', 'TRANSPORT_MODE wifi'])
        s = Connection()
        restore(s, None, None)
        self.assertEqual(s.calls, ['CLEAR'])


if __name__ == '__main__':
    unittest.main()
