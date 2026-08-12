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

    suspend fun getBatchTrendData(days: Int): List<BatchTrendSummary> {
        val cutoffTime = System.currentTimeMillis() - (days * 24 * 60 * 60 * 1000L)
        val readings = dao.getReadingsSince(cutoffTime)

        if (readings.isEmpty()) return emptyList()

        val dateFormat = if (days <= 1) {
            SimpleDateFormat("HH:00", Locale.getDefault())
        } else {
            SimpleDateFormat("MM/dd", Locale.getDefault())
        }

        val grouped = readings.groupBy { dateFormat.format(Date(it.timestamp)) }

        return grouped.map { (timeLabel, items) ->
            val avg = items.map { it.value }.average().toFloat()
            BatchTrendSummary(
                timeLabel = timeLabel,
                avgValue = avg,
                count = items.size
            )
        }
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
