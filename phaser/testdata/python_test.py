import importlib.util
import os
import unittest

from python.runfiles import runfiles


def _load_extension():
    finder = runfiles.Create()
    relative = "phaser/testdata/test_message_phaser_py.so"
    candidates = []
    if finder is not None:
        for prefix in ("_main/", "phaser/"):
            found = finder.Rlocation(prefix + relative)
            if found:
                candidates.append(found)
    for path in candidates:
        if path and os.path.isfile(path):
            spec = importlib.util.spec_from_file_location(
                "test_message_phaser_py", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module
    raise AssertionError("phaser extension not found; tried %s" % candidates)


class PhaserPythonTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.phaser = _load_extension()

    def test_getters_setters_and_round_trip(self):
        p = self.phaser
        msg = p.TestMessage()
        self.assertFalse(msg.has_x())
        msg.set_x(0)
        self.assertTrue(msg.has_x())
        self.assertEqual(msg.x(), 0)
        msg.clear_x()
        self.assertFalse(msg.has_x())

        msg.set_y(-5)
        msg.set_s("hi")
        msg.set_buffer(b"a\0b")
        msg.set_e(p.EnumTest.FOO)
        msg.set_fl(1.5)
        msg.set_db(2.5)
        self.assertEqual(msg.y(), -5)
        self.assertEqual(msg.s(), "hi")
        self.assertEqual(msg.buffer(), b"a\0b")
        self.assertEqual(msg.e(), p.EnumTest.FOO)
        self.assertTrue(msg.has_e())
        self.assertEqual(msg.fl(), 1.5)
        self.assertEqual(msg.db(), 2.5)

        msg.add_vi32(3)
        msg.add_vi32(4)
        msg.set_vi32(1, 9)
        self.assertEqual(msg.vi32_size(), 2)
        self.assertEqual(msg.vi32(0), 3)
        self.assertEqual(msg.vi32(1), 9)
        msg.add_vstr("ab")
        msg.set_vstr(0, "z")
        self.assertEqual(msg.vstr(0), "z")

        nested = msg.mutable_m()
        nested.set_str("nested")
        nested.set_f(-2)
        nested.add_ev(p.EnumTest.BAR)
        self.assertTrue(msg.has_m())
        self.assertEqual(msg.m().str(), "nested")
        self.assertEqual(msg.m().f(), -2)
        self.assertEqual(msg.m().ev(0), p.EnumTest.BAR)

        child = msg.add_vm()
        child.set_str("e0")
        msg.mutable_vm(0).set_str("e1")
        self.assertEqual(msg.vm_size(), 1)
        self.assertEqual(msg.vm(0).str(), "e1")

        self.assertEqual(msg.u1_case(), 0)
        msg.set_u1a(7)
        self.assertEqual(msg.u1_case(), 107)
        self.assertEqual(msg.u1a(), 7)
        self.assertTrue(msg.has_u1a())
        msg.set_u1b(8)
        self.assertEqual(msg.u1_case(), 108)
        self.assertFalse(msg.has_u1a())
        self.assertEqual(msg.u1b(), 8)

        msg.set_u2b("zz")
        self.assertEqual(msg.u2_case(), 110)
        self.assertEqual(msg.u2b(), "zz")
        msg.set_u2a(5)
        self.assertEqual(msg.u2_case(), 109)
        self.assertFalse(msg.has_u2b())
        self.assertEqual(msg.u2a(), 5)

        arm = msg.mutable_u3b()
        arm.set_str("arm")
        self.assertEqual(msg.u3_case(), 112)
        self.assertEqual(msg.u3b().str(), "arm")

        entry = msg.add_values()
        entry.set_key("k")
        entry.set_value(3)
        self.assertEqual(msg.values_size(), 1)
        self.assertEqual(msg.values(0).key(), "k")
        self.assertEqual(msg.values(0).value(), 3)

        self.assertFalse(msg.has_any())
        packed = msg.mutable_any()
        packed.set_type_url("type.googleapis.com/foo.bar.InnerMessage")
        packed.set_value(b"\x00\x01")
        self.assertTrue(msg.has_any())
        self.assertEqual(
            packed.type_url(), "type.googleapis.com/foo.bar.InnerMessage")
        self.assertEqual(packed.value(), b"\x00\x01")

        self.assertGreater(msg.ByteSizeLong(), 0)
        self.assertIn("nested", msg.DebugString())
        blob = msg.SerializeAsString()
        self.assertIsInstance(blob, bytes)
        copy = p.TestMessage(1024)
        self.assertTrue(copy.ParseFromString(blob))
        self.assertEqual(copy.s(), "hi")
        self.assertEqual(copy.buffer(), b"a\0b")
        self.assertEqual(copy.e(), p.EnumTest.FOO)
        self.assertEqual(copy.m().str(), "nested")
        self.assertEqual(copy.vm(0).str(), "e1")
        self.assertEqual(copy.values(0).key(), "k")
        self.assertEqual(copy.any().type_url(), packed.type_url())
        self.assertGreater(copy.ByteSizeLong(), 0)
        copy.Clear()
        self.assertFalse(copy.has_s())
        self.assertEqual(copy.vstr_size(), 0)
        self.assertFalse(copy.has_any())


if __name__ == "__main__":
    unittest.main()
