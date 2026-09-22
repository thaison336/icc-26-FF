package com.example.blewearable.data

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import kotlinx.coroutines.flow.Flow

@Dao
interface SensorDataDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertReading(reading: SensorDataEntity)

    @Query("SELECT * FROM sensor_readings ORDER BY timestamp DESC LIMIT 100")
    fun getRecentReadings(): Flow<List<SensorDataEntity>>

    @Query("SELECT COUNT(*) FROM sensor_readings")
    fun getTotalCount(): Flow<Int>

    @Query("DELETE FROM sensor_readings")
    suspend fun clearAll()

    @Query("SELECT * FROM sensor_readings WHERE timestamp >= :sinceTimestamp ORDER BY timestamp ASC")
    suspend fun getReadingsSince(sinceTimestamp: Long): List<SensorDataEntity>

    @Query("SELECT * FROM sensor_readings ORDER BY timestamp ASC")
    suspend fun getAllReadings(): List<SensorDataEntity>
}
