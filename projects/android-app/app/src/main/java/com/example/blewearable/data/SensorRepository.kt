package com.example.blewearable.data

import kotlinx.coroutines.flow.Flow
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SensorRepository(private val dao: SensorDataDao) {

    val recentReadings: Flow<List<SensorDataEntity>> = dao.getRecentReadings()
    val totalCount: Flow<Int> = dao.getTotalCount()

    suspend fun saveReading(value: Float, rawPayload: String, deviceName: String) {
        val entity = SensorDataEntity(
            value = value,
            rawPayload = rawPayload,
            deviceName = deviceName
        )
        dao.insertReading(entity)
    }

    suspend fun clearHistory() {
        dao.clearAll()
    }

    suspend fun getBatchTrendData(durationMs: Long): List<BatchTrendSummary> {
        val cutoffTime = System.currentTimeMillis() - durationMs
        val readings = dao.getReadingsSince(cutoffTime)

        if (readings.isEmpty()) return emptyList()

        // Chọn khoảng gom nhóm (bucketMs) và định dạng ngày phù hợp theo độ dài thời gian
        val (bucketMs, dateFormatPattern) = when {
            durationMs <= 2 * 60 * 1000L -> Pair(5 * 1000L, "HH:mm:ss")       // <= 2 phút: gom mỗi 5 giây
            durationMs <= 10 * 60 * 1000L -> Pair(15 * 1000L, "HH:mm:ss")     // <= 10 phút: gom mỗi 15 giây
            durationMs <= 60 * 60 * 1000L -> Pair(2 * 60 * 1000L, "HH:mm")    // <= 1 giờ: gom mỗi 2 phút
            durationMs <= 6 * 60 * 60 * 1000L -> Pair(10 * 60 * 1000L, "HH:mm")// <= 6 giờ: gom mỗi 10 phút
            durationMs <= 24 * 60 * 60 * 1000L -> Pair(60 * 60 * 1000L, "HH:00") // <= 24 giờ: gom mỗi 1 giờ
            durationMs <= 7 * 24 * 60 * 60 * 1000L -> Pair(24 * 60 * 60 * 1000L, "MM/dd") // <= 7 ngày: gom mỗi ngày
            else -> Pair(24 * 60 * 60 * 1000L, "MM/dd")                       // > 7 ngày: gom mỗi ngày
        }

        val sdf = SimpleDateFormat(dateFormatPattern, Locale.getDefault())

        val grouped = readings.groupBy { reading ->
            (reading.timestamp / bucketMs) * bucketMs
        }

        return grouped.toSortedMap().map { (bucketTime, items) ->
            val validItems = items.filter { it.value > 0f }
            val avg = if (validItems.isNotEmpty()) {
                validItems.map { it.value }.average().toFloat()
            } else {
                items.map { it.value }.average().toFloat()
            }
            BatchTrendSummary(
                timeLabel = sdf.format(Date(bucketTime)),
                avgValue = avg,
                count = items.size
            )
        }
    }

    suspend fun getBatchTrendData(days: Int): List<BatchTrendSummary> {
        return getBatchTrendData(days * 24 * 60 * 60 * 1000L)
    }

    suspend fun exportDataAsCsv(): String {
        val readings = dao.getAllReadings()
        val sb = StringBuilder()
        sb.append("ID,Timestamp,DateTime,DeviceName,Value,RawPayload\n")
        val sdf = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

        readings.forEach { r ->
            val dateStr = sdf.format(Date(r.timestamp))
            val escapedPayload = r.rawPayload.replace("\"", "\"\"")
            sb.append("${r.id},${r.timestamp},\"$dateStr\",\"${r.deviceName}\",${r.value},\"$escapedPayload\"\n")
        }
        return sb.toString()
    }

    suspend fun exportDataAsJson(): String {
        val readings = dao.getAllReadings()
        val sb = StringBuilder()
        sb.append("[\n")
        val sdf = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())
        readings.forEachIndexed { index, r ->
            val dateStr = sdf.format(Date(r.timestamp))
            sb.append("  {\n")
            sb.append("    \"id\": ${r.id},\n")
            sb.append("    \"timestamp\": ${r.timestamp},\n")
            sb.append("    \"dateTime\": \"$dateStr\",\n")
            sb.append("    \"deviceName\": \"${r.deviceName}\",\n")
            sb.append("    \"value\": ${r.value},\n")
            sb.append("    \"rawPayload\": \"${r.rawPayload.replace("\"", "\\\"")}\"\n")
            sb.append("  }${if (index < readings.size - 1) "," else ""}\n")
        }
        sb.append("]")
        return sb.toString()
    }
}
