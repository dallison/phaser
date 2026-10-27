import importlib.util
import os
import unittest

from python.runfiles import runfiles


def _rlocation(*relative_paths):
    finder = runfiles.Create()
    if finder is None:
        return None
    for relative in relative_paths:
        for prefix in ("_main/", "phaser/"):
            found = finder.Rlocation(prefix + relative)
            if found and os.path.isfile(found):
                return found
    return None


def _load_extension(module_name, *relative_paths):
    path = _rlocation(*relative_paths)
    if path is None:
        raise AssertionError("%s not found (%s)" % (module_name, relative_paths))
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PhaserBankPyTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.bank = _load_extension(
            "phaser_bank_py",
            "phaser/runtime/phaser_bank_py_testlib.so",
            "phaser/runtime/libphaser_bank_py_testlib.dylib",
        )

    def test_plugin_submodules(self):
        bank = self.bank
        self.assertEqual(bank.arrow.OP_COLLECT_TYPES, "collect-types")
        self.assertEqual(bank.avro.FORMAT, bank.FORMAT_AVRO)
        self.assertEqual(bank.parquet.FORMAT, bank.FORMAT_PARQUET)
        self.assertEqual(bank.ros.FORMAT, bank.FORMAT_ROS)
        self.assertEqual(bank.protobuf.FORMAT, bank.FORMAT_PROTOBUF)
        self.assertEqual(
            bank.stamped_ros_avro.FORMAT, bank.FORMAT_STAMPED_ROS_AVRO)
        ops = bank.arrow.operations()
        self.assertIn("collect-types", ops)
        self.assertIn("write", ops)

    def test_conversion_graph(self):
        bank = self.bank
        formats = set(bank.registered_formats())
        self.assertIn(bank.FORMAT_PHASER, formats)
        self.assertIn(bank.FORMAT_ROS, formats)
        self.assertTrue(
            bank.has_conversion(bank.FORMAT_PHASER, bank.FORMAT_ROS))
        edges = bank.conversion_edges()
        self.assertTrue(any(e[0] == bank.FORMAT_PHASER for e in edges))

    def test_message_properties(self):
        bank = self.bank
        props = set(bank.message_properties())
        self.assertIn(bank.PROP_ROS_DATATYPE, props)
        self.assertIn(bank.PROP_PROTOBUF_FULL_NAME, props)

    def test_ros_metadata(self):
        bank = self.bank
        full_name = "example_msgs.proto.Bool"
        self.assertEqual(bank.ros_datatype(full_name), "example_msgs/Bool")
        self.assertEqual(
            bank.message_property(bank.PROP_ROS_DATATYPE, full_name),
            "example_msgs/Bool",
        )


if __name__ == "__main__":
    unittest.main()
