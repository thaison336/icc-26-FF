package com.example.blewearable.ble

import java.util.UUID

object BleConstants {
    // Default Custom Service & Characteristic UUIDs (common for ESP32/HM-10/nRF modules)
    // Replace with your wearable device's specific UUIDs if different
    val CUSTOM_SERVICE_UUID: UUID = UUID.fromString("0000FFE0-0000-1000-8000-00805F9B34FB")
    val CUSTOM_CHARACTERISTIC_UUID: UUID = UUID.fromString("0000FFE1-0000-1000-8000-00805F9B34FB")

    // Standard Client Characteristic Configuration Descriptor for enabling Notifications
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}
