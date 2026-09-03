import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.jump_phase_e_replay import JumpInertialModel, JumpState


class JumpPhaseEModelTests(unittest.TestCase):
    def test_direct_landing_without_start_stays_native(self):
        model = JumpInertialModel()
        model.step(0.05, jump_start=False, landing=False,
                   signal_valid=True, falling_speed=-8.0)
        result = model.step(0.05, jump_start=False, landing=True,
                            signal_valid=True, falling_speed=-12.0)
        self.assertEqual(result.state, JumpState.NATIVE_ONLY)
        self.assertFalse(result.source_valid)
        self.assertEqual(result.event_epoch, 0)
        self.assertEqual(result.angle_deg, 0.0)

    def test_start_level_is_edge_latched_and_drives_full_fsm(self):
        model = JumpInertialModel()
        first = model.step(0.05, jump_start=True, landing=False,
                           signal_valid=True, falling_speed=10.0)
        self.assertEqual(first.state, JumpState.RISING)
        self.assertTrue(first.source_valid)
        self.assertEqual(first.event_epoch, 1)

        held = model.step(0.05, jump_start=True, landing=False,
                          signal_valid=True, falling_speed=8.0)
        self.assertEqual(held.event_epoch, 1)
        self.assertEqual(held.state, JumpState.RISING)

        apex = model.step(0.05, jump_start=False, landing=False,
                          signal_valid=True, falling_speed=0.1)
        self.assertEqual(apex.state, JumpState.APEX)
        falling = model.step(0.05, jump_start=False, landing=False,
                             signal_valid=True, falling_speed=-1.0)
        self.assertEqual(falling.state, JumpState.FALLING)
        landed = model.step(0.05, jump_start=False, landing=True,
                            signal_valid=True, falling_speed=-12.0)
        self.assertEqual(landed.state, JumpState.LANDING_TAIL)
        self.assertTrue(landed.source_valid)

        outputs = [landed]
        for _ in range(200):
            outputs.append(model.step(0.02, jump_start=False, landing=False,
                                      signal_valid=True, falling_speed=-12.0))
        self.assertTrue(all(math.isfinite(x.angle_deg) and
                            math.isfinite(x.angular_velocity_deg_s)
                            for x in outputs))
        self.assertLessEqual(max(abs(x.angle_deg) for x in outputs), 8.0)
        self.assertEqual(outputs[-1].state, JumpState.NATIVE_ONLY)
        self.assertFalse(outputs[-1].source_valid)

    def test_invalid_signal_cancels_armed_epoch_fail_closed(self):
        model = JumpInertialModel()
        model.step(0.05, jump_start=True, landing=False,
                   signal_valid=True, falling_speed=8.0)
        result = model.step(0.05, jump_start=False, landing=False,
                            signal_valid=False, falling_speed=float("nan"))
        self.assertEqual(result.state, JumpState.NATIVE_ONLY)
        self.assertFalse(result.source_valid)
        self.assertEqual(result.angle_deg, 0.0)
        self.assertEqual(result.angular_velocity_deg_s, 0.0)

    def test_event_impulses_change_velocity_after_interval_not_past_angle(self):
        landing_model = JumpInertialModel()
        control_model = JumpInertialModel()
        for model in (landing_model, control_model):
            model.step(0.05, jump_start=True, landing=False,
                       signal_valid=True, falling_speed=10.0)
            model.step(0.05, jump_start=False, landing=False,
                       signal_valid=True, falling_speed=0.0)
            model.step(0.05, jump_start=False, landing=False,
                       signal_valid=True, falling_speed=-4.0)

        landed = landing_model.step(
            0.08, jump_start=False, landing=True,
            signal_valid=True, falling_speed=-12.0)
        control = control_model.step(
            0.08, jump_start=False, landing=False,
            signal_valid=True, falling_speed=-12.0)
        self.assertAlmostEqual(landed.angle_deg, control.angle_deg, places=9)
        self.assertNotEqual(landed.angular_velocity_deg_s,
                            control.angular_velocity_deg_s)

    def test_hitch_and_extreme_signal_remain_bounded(self):
        model = JumpInertialModel()
        result = model.step(1.0, jump_start=True, landing=False,
                            signal_valid=True, falling_speed=10000.0)
        self.assertTrue(math.isfinite(result.angle_deg))
        self.assertTrue(math.isfinite(result.angular_velocity_deg_s))
        self.assertLessEqual(abs(result.angle_deg), 8.0)
        self.assertLessEqual(abs(result.angular_velocity_deg_s), 120.0)


if __name__ == "__main__":
    unittest.main()
