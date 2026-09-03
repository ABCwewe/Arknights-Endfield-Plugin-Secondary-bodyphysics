#!/usr/bin/env python
"""Phase E offline Jump FSM + bounded single-axis oscillator replay.

This module never loads or modifies the game. It consumes the Phase-D CSV,
groups multi-clip rows by serial, and writes one derived row per sample.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
from collections import Counter, OrderedDict
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Iterable


class JumpState(str, Enum):
    NATIVE_ONLY = "NativeOnly"
    ARMED = "Armed"
    RISING = "Rising"
    APEX = "Apex"
    FALLING = "Falling"
    LANDING_TAIL = "LandingTail"


@dataclass(frozen=True)
class JumpModelParams:
    natural_frequency_hz: float = 2.2
    damping_ratio: float = 0.48
    acceleration_to_angle_deg: float = 0.10
    acceleration_filter_tau_s: float = 0.08
    acceleration_clamp: float = 40.0
    takeoff_speed_threshold: float = 0.5
    apex_enter_speed: float = 0.25
    falling_enter_speed: float = -0.25
    armed_timeout_s: float = 0.30
    takeoff_velocity_impulse_deg_s: float = -24.0
    landing_velocity_gain_deg_s_per_speed: float = 4.0
    landing_velocity_impulse_limit_deg_s: float = 56.0
    max_angle_deg: float = 8.0
    max_angular_velocity_deg_s: float = 120.0
    max_dt_s: float = 0.10
    max_substep_s: float = 1.0 / 240.0
    min_tail_s: float = 0.12
    max_tail_s: float = 1.50
    settle_angle_deg: float = 0.04
    settle_velocity_deg_s: float = 0.25


@dataclass(frozen=True)
class JumpStepResult:
    state: JumpState
    source_valid: bool
    event_epoch: int
    angle_deg: float
    angular_velocity_deg_s: float
    filtered_acceleration: float
    start_edge: bool
    landing_edge: bool
    accepted_start: bool
    accepted_landing: bool
    direct_landing: bool


class JumpInertialModel:
    def __init__(self, params: JumpModelParams | None = None) -> None:
        self.params = params or JumpModelParams()
        self.reset()

    def reset(self) -> None:
        self.state = JumpState.NATIVE_ONLY
        self.event_epoch = 0
        self.angle_deg = 0.0
        self.angular_velocity_deg_s = 0.0
        self.filtered_acceleration = 0.0
        self._previous_speed = 0.0
        self._has_previous_speed = False
        self._previous_start = False
        self._previous_landing = False
        self._state_elapsed_s = 0.0

    def _cancel_fail_closed(self) -> None:
        previous_start = self._previous_start
        previous_landing = self._previous_landing
        epoch = self.event_epoch
        self.reset()
        self._previous_start = previous_start
        self._previous_landing = previous_landing
        self.event_epoch = epoch

    def _integrate(self, dt_s: float, forcing_acceleration: float) -> None:
        p = self.params
        dt_s = min(max(dt_s, 0.0), p.max_dt_s)
        if dt_s <= 0.0:
            return
        substeps = max(1, math.ceil(dt_s / p.max_substep_s))
        h = dt_s / substeps
        omega = 2.0 * math.pi * p.natural_frequency_hz
        for _ in range(substeps):
            acceleration_deg_s2 = (
                -2.0 * p.damping_ratio * omega *
                self.angular_velocity_deg_s
                - omega * omega * self.angle_deg
                - omega * omega * p.acceleration_to_angle_deg *
                forcing_acceleration
            )
            self.angular_velocity_deg_s += acceleration_deg_s2 * h
            self.angular_velocity_deg_s = max(
                -p.max_angular_velocity_deg_s,
                min(p.max_angular_velocity_deg_s,
                    self.angular_velocity_deg_s),
            )
            self.angle_deg += self.angular_velocity_deg_s * h
            self.angle_deg = max(-p.max_angle_deg,
                                 min(p.max_angle_deg, self.angle_deg))

    def step(
        self,
        dt_s: float,
        *,
        jump_start: bool,
        landing: bool,
        signal_valid: bool,
        falling_speed: float,
    ) -> JumpStepResult:
        p = self.params
        dt_s = min(max(float(dt_s), 0.0), p.max_dt_s)
        start_edge = bool(jump_start) and not self._previous_start
        landing_edge = bool(landing) and not self._previous_landing
        self._previous_start = bool(jump_start)
        self._previous_landing = bool(landing)
        accepted_start = False
        accepted_landing = False
        direct_landing = False

        if not signal_valid or not math.isfinite(falling_speed):
            self._cancel_fail_closed()
            return self._result(start_edge, landing_edge, False, False, False)

        speed = max(-100.0, min(100.0, float(falling_speed)))
        raw_acceleration = 0.0
        if self._has_previous_speed and dt_s > 0.0:
            raw_acceleration = (speed - self._previous_speed) / dt_s
            raw_acceleration = max(-p.acceleration_clamp,
                                   min(p.acceleration_clamp,
                                       raw_acceleration))
        self._previous_speed = speed
        self._has_previous_speed = True

        if dt_s > 0.0:
            alpha = 1.0 - math.exp(
                -dt_s / max(p.acceleration_filter_tau_s, 1e-6))
            self.filtered_acceleration += alpha * (
                raw_acceleration - self.filtered_acceleration)

        # Advance the state that existed over the preceding interval. Current
        # sample edges are processed afterwards, so an impulse cannot rewrite
        # the time before the event was observed.
        previous_active_epoch = self.state in {
            JumpState.ARMED,
            JumpState.RISING,
            JumpState.APEX,
            JumpState.FALLING,
        }
        forcing = self.filtered_acceleration if previous_active_epoch else 0.0
        self._integrate(dt_s, forcing)
        self._state_elapsed_s += dt_s

        if self.state == JumpState.LANDING_TAIL:
            settled = (
                self._state_elapsed_s >= p.min_tail_s
                and abs(self.angle_deg) <= p.settle_angle_deg
                and abs(self.angular_velocity_deg_s) <=
                    p.settle_velocity_deg_s
            )
            if settled or self._state_elapsed_s >= p.max_tail_s:
                self.state = JumpState.NATIVE_ONLY
                self.angle_deg = 0.0
                self.angular_velocity_deg_s = 0.0
                self.filtered_acceleration = 0.0

        if self.state == JumpState.ARMED and (
                self._state_elapsed_s >= p.armed_timeout_s):
            self._cancel_fail_closed()

        if start_edge:
            self.event_epoch += 1
            accepted_start = True
            self.state = JumpState.ARMED
            self._state_elapsed_s = 0.0
            self.angular_velocity_deg_s += p.takeoff_velocity_impulse_deg_s
            self.angular_velocity_deg_s = max(
                -p.max_angular_velocity_deg_s,
                min(p.max_angular_velocity_deg_s,
                    self.angular_velocity_deg_s),
            )

        if self.state == JumpState.ARMED and speed > p.takeoff_speed_threshold:
            self.state = JumpState.RISING
            self._state_elapsed_s = 0.0
        elif self.state == JumpState.RISING and speed <= p.apex_enter_speed:
            self.state = JumpState.APEX
            self._state_elapsed_s = 0.0
        elif self.state == JumpState.APEX and speed < p.falling_enter_speed:
            self.state = JumpState.FALLING
            self._state_elapsed_s = 0.0

        active_epoch = self.state in {
            JumpState.ARMED,
            JumpState.RISING,
            JumpState.APEX,
            JumpState.FALLING,
        }
        if landing_edge:
            if active_epoch:
                accepted_landing = True
                self.state = JumpState.LANDING_TAIL
                self._state_elapsed_s = 0.0
                impulse = min(
                    p.landing_velocity_impulse_limit_deg_s,
                    abs(speed) * p.landing_velocity_gain_deg_s_per_speed,
                )
                self.angular_velocity_deg_s -= impulse
                self.angular_velocity_deg_s = max(
                    -p.max_angular_velocity_deg_s,
                    min(p.max_angular_velocity_deg_s,
                        self.angular_velocity_deg_s),
                )
            elif self.state == JumpState.NATIVE_ONLY:
                direct_landing = True

        if self.state == JumpState.NATIVE_ONLY:
            self.angle_deg = 0.0
            self.angular_velocity_deg_s = 0.0

        return self._result(start_edge, landing_edge, accepted_start,
                            accepted_landing, direct_landing)

    def _result(self, start_edge: bool, landing_edge: bool,
                accepted_start: bool, accepted_landing: bool,
                direct_landing: bool) -> JumpStepResult:
        return JumpStepResult(
            state=self.state,
            source_valid=self.state != JumpState.NATIVE_ONLY,
            event_epoch=self.event_epoch,
            angle_deg=self.angle_deg,
            angular_velocity_deg_s=self.angular_velocity_deg_s,
            filtered_acceleration=self.filtered_acceleration,
            start_edge=start_edge,
            landing_edge=landing_edge,
            accepted_start=accepted_start,
            accepted_landing=accepted_landing,
            direct_landing=direct_landing,
        )


def _as_int(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(row.get(key, "") or default)
    except (TypeError, ValueError):
        return default


def _as_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, ""))
    except (TypeError, ValueError):
        return float("nan")


def load_phase_d_samples(path: Path) -> list[dict[str, object]]:
    grouped: OrderedDict[int, dict[str, object]] = OrderedDict()
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            serial = _as_int(row, "serial", -1)
            if serial < 0:
                continue
            if serial not in grouped:
                grouped[serial] = {"row": row, "clips": []}
            clip = row.get("clip_name", "")
            if clip:
                grouped[serial]["clips"].append(clip)

    samples: list[dict[str, object]] = []
    for serial, group in grouped.items():
        row = group["row"]
        clips = group["clips"]
        samples.append({
            "serial": serial,
            "elapsed_ms": _as_int(row, "elapsed_ms"),
            "jump_start": any("jump_start" in str(x).lower()
                              for x in clips),
            "landing": any("jump_land" in str(x).lower()
                           for x in clips),
            "signal_valid": bool(
                _as_int(row, "phase_d_active")
                and _as_int(row, "movement_identity_valid")
                and _as_int(row, "falling_speed_valid")
            ),
            "falling_speed": _as_float(row, "falling_speed"),
        })
    return samples


def replay_samples(samples: Iterable[dict[str, object]],
                   params: JumpModelParams | None = None
                   ) -> tuple[list[dict[str, object]], dict[str, object]]:
    model = JumpInertialModel(params)
    output: list[dict[str, object]] = []
    previous_ms: int | None = None
    for sample in samples:
        elapsed_ms = int(sample["elapsed_ms"])
        dt_s = 0.0 if previous_ms is None else max(
            0.0, (elapsed_ms - previous_ms) / 1000.0)
        previous_ms = elapsed_ms
        result = model.step(
            dt_s,
            jump_start=bool(sample["jump_start"]),
            landing=bool(sample["landing"]),
            signal_valid=bool(sample["signal_valid"]),
            falling_speed=float(sample["falling_speed"]),
        )
        output.append({
            **sample,
            "dt_s": dt_s,
            "state": result.state.value,
            "source_valid": int(result.source_valid),
            "event_epoch": result.event_epoch,
            "start_edge": int(result.start_edge),
            "landing_edge": int(result.landing_edge),
            "accepted_start": int(result.accepted_start),
            "accepted_landing": int(result.accepted_landing),
            "direct_landing": int(result.direct_landing),
            "filtered_acceleration": result.filtered_acceleration,
            "angle_deg": result.angle_deg,
            "angular_velocity_deg_s": result.angular_velocity_deg_s,
        })

    angles = [float(x["angle_deg"]) for x in output]
    velocities = [float(x["angular_velocity_deg_s"]) for x in output]
    steps = [abs(b - a) for a, b in zip(angles, angles[1:])]
    summary: dict[str, object] = {
        "samples": len(output),
        "accepted_starts": sum(int(x["accepted_start"]) for x in output),
        "accepted_landings": sum(int(x["accepted_landing"])
                                 for x in output),
        "direct_landings": sum(int(x["direct_landing"]) for x in output),
        "event_epochs": max((int(x["event_epoch"]) for x in output),
                            default=0),
        "state_samples": dict(Counter(str(x["state"]) for x in output)),
        "max_abs_angle_deg": max((abs(x) for x in angles), default=0.0),
        "max_abs_angular_velocity_deg_s": max(
            (abs(x) for x in velocities), default=0.0),
        "max_sample_angle_step_deg": max(steps, default=0.0),
        "nonfinite_outputs": sum(
            not math.isfinite(a) or not math.isfinite(v)
            for a, v in zip(angles, velocities)
        ),
    }
    return output, summary


def write_replay_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = [
        "serial", "elapsed_ms", "dt_s", "jump_start", "landing",
        "signal_valid", "falling_speed", "state", "source_valid",
        "event_epoch", "start_edge", "landing_edge", "accepted_start",
        "accepted_landing", "direct_landing", "filtered_acceleration",
        "angle_deg", "angular_velocity_deg_s",
    ]
    temp = path.with_suffix(path.suffix + ".tmp")
    path.parent.mkdir(parents=True, exist_ok=True)
    with temp.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
        stream.flush()
        os.fsync(stream.fileno())
    temp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    samples = load_phase_d_samples(args.input)
    if not samples:
        raise SystemExit("no Phase D samples")
    rows, summary = replay_samples(samples)
    write_replay_csv(args.output, rows)
    summary["output"] = str(args.output)
    print(json.dumps(summary, ensure_ascii=False, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
