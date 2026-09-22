package com.example.blewearable.ble

data class BleDeviceModel(
    val name: String,
    val address: String,
    val rssi: Int,
    val lastSeenTimestamp: Long = System.currentTimeMillis()
)
