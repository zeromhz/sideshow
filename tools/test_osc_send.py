import struct
import unittest

from osc_send import decode_message, encode_message, parse_value


class EncodeTest(unittest.TestCase):
    def test_no_args_pads_address_and_type_tags(self):
        self.assertEqual(encode_message("/a", []), b"/a\0\0,\0\0\0")

    def test_four_char_string_gets_a_full_padding_word(self):
        data = encode_message("/abc", ["abcd"])
        self.assertEqual(data, b"/abc\0\0\0\0,s\0\0abcd\0\0\0\0")

    def test_int_and_float_are_big_endian(self):
        data = encode_message("/x", [7, 0.5])
        self.assertEqual(data, b"/x\0\0,if\0" + struct.pack(">i", 7) + struct.pack(">f", 0.5))

    def test_round_trip(self):
        address, args = decode_message(encode_message("/sideshow/pong", ["panel-01", "Foo.milk", 59.5]))
        self.assertEqual(address, "/sideshow/pong")
        self.assertEqual(args, ["panel-01", "Foo.milk", 59.5])

    def test_rejects_unsupported_types(self):
        with self.assertRaises(TypeError):
            encode_message("/x", [b"bytes"])


class ParseValueTest(unittest.TestCase):
    def test_toggles_accept_int_and_float_text(self):
        self.assertEqual(parse_value("blackout", "1"), [1])
        self.assertEqual(parse_value("lock", "1.0"), [1])
        self.assertEqual(parse_value("hardcut", "0.5"), [0])
        self.assertEqual(parse_value("blackout", "0"), [0])

    def test_floats_and_strings(self):
        self.assertEqual(parse_value("master", "0.5"), [0.5])
        self.assertEqual(parse_value("duration", "10"), [10.0])
        self.assertEqual(parse_value("playlist", "mellow"), ["mellow"])
        self.assertEqual(parse_value("next", None), [])

    def test_bad_input_raises_value_error(self):
        for command, text in [("blackout", "on"), ("master", "loud"), ("lock", "inf"),
                              ("master", None), ("next", "1")]:
            with self.assertRaises(ValueError):
                parse_value(command, text)


if __name__ == "__main__":
    unittest.main()
