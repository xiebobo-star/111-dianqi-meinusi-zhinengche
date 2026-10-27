import unittest

from virtual_bds300 import VirtualBDS300


class VirtualBDS300Test(unittest.TestCase):
    def test_piecewise_calibration(self):
        model = VirtualBDS300()
        self.assertEqual(model.pulse_to_angle(1000, 1000, 1500, 2000), 0)
        self.assertEqual(model.pulse_to_angle(1500, 1000, 1500, 2000), 90)
        self.assertEqual(model.pulse_to_angle(2000, 1000, 1500, 2000), 180)
        self.assertEqual(model.pulse_to_angle(1250, 1000, 1500, 2000), 45)
        self.assertEqual(model.pulse_to_angle(1750, 1000, 1500, 2000), 135)

    def test_telemetry_changes_target(self):
        model = VirtualBDS300()
        status = model.apply_line(
            "BDS300 angle=30 min_us=1000 center_us=1500 max_us=2000 "
            "enable=1 pulse_us=1166 duty=5830"
        )
        self.assertIsNotNone(status)
        self.assertAlmostEqual(status["target_angle_deg"], 29.88, places=2)
        self.assertEqual(status["pulse_us"], 1166)

    def test_disabled_output(self):
        model = VirtualBDS300()
        status = model.apply_line(
            "BDS300 angle=30 min_us=1000 center_us=1500 max_us=2000 "
            "enable=0 pulse_us=0 duty=0"
        )
        self.assertFalse(status["enabled"])
        self.assertEqual(status["pulse_us"], 0)


if __name__ == "__main__":
    unittest.main()
