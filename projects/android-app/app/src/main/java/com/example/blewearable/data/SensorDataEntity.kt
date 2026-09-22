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

enum class TimeRangeUnit(val displayName: String, val multiplierMs: Long) {
    MINUTES("Minutes", 60 * 1000L),
    HOURS("Hours", 60 * 60 * 1000L),
    DAYS("Days", 24 * 60 * 60 * 1000L)
}

data class TrendTimeRange(
    val amount: Int,
    val unit: TimeRangeUnit,
    val shortLabel: String
) {
    val durationMs: Long get() = amount * unit.multiplierMs
    val fullDescription: String
        get() {
            val unitStr = if (amount == 1) {
                when (unit) {
                    TimeRangeUnit.MINUTES -> "minute"
                    TimeRangeUnit.HOURS -> "hour"
                    TimeRangeUnit.DAYS -> "day"
                }
            } else {
                unit.displayName.lowercase()
            }
            return "Last $amount $unitStr"
        }

    companion object {
        val RANGE_1_MIN = TrendTimeRange(1, TimeRangeUnit.MINUTES, "1m")
        val RANGE_5_MIN = TrendTimeRange(5, TimeRangeUnit.MINUTES, "5m")
        val RANGE_1_HOUR = TrendTimeRange(1, TimeRangeUnit.HOURS, "1h")
        val RANGE_24_HOURS = TrendTimeRange(24, TimeRangeUnit.HOURS, "24h")
        val RANGE_7_DAYS = TrendTimeRange(7, TimeRangeUnit.DAYS, "7d")
        val RANGE_30_DAYS = TrendTimeRange(30, TimeRangeUnit.DAYS, "30d")

        val PRESETS = listOf(
            RANGE_1_MIN,
            RANGE_5_MIN,
            RANGE_1_HOUR,
            RANGE_24_HOURS,
            RANGE_7_DAYS,
            RANGE_30_DAYS
        )
    }
}
