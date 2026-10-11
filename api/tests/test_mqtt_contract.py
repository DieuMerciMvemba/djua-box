"""Regression tests for the ESP32 MQTT-to-API data contract."""

import json
import unittest
from types import SimpleNamespace

from app import main


class MqttContractTests(unittest.TestCase):
    def setUp(self) -> None:
        with main.state_lock:
            main.latest_telemetry_by_device.clear()
            main.latest_geofence_by_device.clear()
            main.history.clear()
            main.message_sequence = 0

    def publish(self, topic: str, payload: dict[str, object]) -> None:
        message = SimpleNamespace(
            topic=topic,
            payload=json.dumps(payload).encode("utf-8"),
        )
        main.on_message(None, None, message)

    def test_telemetry_preserves_direct_and_calculated_sensor_values(self) -> None:
        payload = {
            "kit_id": "DJUA-KIN-000001",
            "timestamp": "2026-09-08T12:00:00+01:00",
            "timezone": "GMT+1",
            "latitude": -4.3251,
            "longitude": 15.3222,
            "interval_seconds": 1800,
            "battery": {
                "voltage_v": 12.4,
                "current_a": 0.5,
                "power_w": 6.2,
            },
            "solar": {
                "voltage_v": 0.0,
                "current_a": 0.0,
                "power_w": 0.0,
                "energy_interval_wh": 0.0,
            },
            "ac_load": {
                "voltage_v": 230.1,
                "current_a": 1.25,
                "active_power_w": 244.5,
                "apparent_power_va": 287.6,
                "energy_interval_wh": 122.25,
            },
        }
        self.publish("djua/test/DJUA-KIN-000001/telemetry", payload)

        record = main.latest_telemetry_by_device["DJUA-KIN-000001"]
        self.assertEqual(record["message_type"], main.MESSAGE_TYPE_TELEMETRY)
        self.assertEqual(record["data"], payload)
        self.assertNotIn("timestamp_ms", record["data"])

    def test_solar_unavailable_is_distinct_from_valid_zero(self) -> None:
        keys = ("voltage_v", "current_a", "power_w", "energy_interval_wh")
        for value in (None, 0.0):
            payload = {
                "kit_id": "DJUA-KIN-000001",
                "solar": dict.fromkeys(keys, value),
            }
            self.publish("djua/test/DJUA-KIN-000001/telemetry", payload)
            record = main.latest_telemetry_by_device["DJUA-KIN-000001"]
            self.assertEqual(record["data"], payload)
            self.assertEqual(main.latest_telemetry("DJUA-KIN-000001")["data"], payload)
        self.assertIsNone(main.history[-2]["data"]["solar"]["power_w"])
        self.assertEqual(main.history[-1]["data"]["solar"]["power_w"], 0.0)

    def test_geofence_snapshot_and_event_reach_the_api(self) -> None:
        payload = {
            "kit_id": "DJUA-KIN-000001",
            "state": "OUTSIDE",
            "event": "GEOFENCE_EXIT",
            "position_usable": True,
            "distance_m": 600.0,
            "confirmation": {"count": 3, "required": 3},
        }
        self.publish("djua/test/DJUA-KIN-000001/geofence", payload)
        self.publish("djua/test/DJUA-KIN-000001/geofence/events", payload)

        self.assertEqual(len(main.history), 2)
        self.assertEqual(
            main.history[0]["message_type"], main.MESSAGE_TYPE_GEOFENCE
        )
        self.assertEqual(
            main.history[1]["message_type"], main.MESSAGE_TYPE_GEOFENCE_EVENT
        )
        self.assertEqual(
            main.latest_geofence_by_device["DJUA-KIN-000001"]["data"], payload
        )

    def test_non_data_status_topic_is_not_treated_as_sensor_data(self) -> None:
        message = SimpleNamespace(
            topic="djua/test/DJUA-KIN-000001/status",
            payload=b"online",
        )
        main.on_message(None, None, message)
        self.assertFalse(main.history)

if __name__ == "__main__":
    unittest.main()
