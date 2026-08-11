package com.example.blewearable.data

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "sensor_readings")
data class SensorDataEntity(
    @PrimaryKey(autoGenerate = true)
    val id: Long = 0,
    val timestamp: Long = System.currentTimeMillis(),
    val value: Float,
    val rawPayload: String,
    val deviceName: String
)

data class BatchTrendSummary(
    val timeLabel: String,
    val avgValue: Float,
    val count: Int
)
