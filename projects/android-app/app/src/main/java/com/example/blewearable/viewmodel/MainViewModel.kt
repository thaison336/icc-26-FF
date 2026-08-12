package com.example.blewearable.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ble.BleDeviceModel
import com.example.blewearable.ble.BleManager
import com.example.blewearable.data.AppDatabase
import com.example.blewearable.data.BatchTrendSummary
import com.example.blewearable.data.SensorDataEntity
import com.example.blewearable.data.SensorRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.random.Random

class MainViewModel(application: Application) : AndroidViewModel(application) {

    val bleManager = BleManager(application)
    private val repository: SensorRepository

    val connectionState: StateFlow<BleConnectionState> = bleManager.connectionState
    val scannedDevices: StateFlow<List<BleDeviceModel>> = bleManager.scannedDevices

    val recentReadings = MutableStateFlow<List<SensorDataEntity>>(emptyList())
    val totalCount = MutableStateFlow(0)

    private val _trendData = MutableStateFlow<List<BatchTrendSummary>>(emptyList())
    val trendData: StateFlow<List<BatchTrendSummary>> = _trendData.asStateFlow()

    private val _selectedTimeFrame = MutableStateFlow(30) // 1 day, 7 days, 30 days (default to 30d)
    val selectedTimeFrame: StateFlow<Int> = _selectedTimeFrame.asStateFlow()

    private val _latestReading = MutableStateFlow<Float?>(null)
    val latestReading: StateFlow<Float?> = _latestReading.asStateFlow()

    private val _realSpO2 = MutableStateFlow<Float?>(null)
    val realSpO2: StateFlow<Float?> = _realSpO2.asStateFlow()

    private val _realHeartRate = MutableStateFlow<Float?>(null)
    val realHeartRate: StateFlow<Float?> = _realHeartRate.asStateFlow()

    private val _latestRawPayload = MutableStateFlow<String>("")
    val latestRawPayload: StateFlow<String> = _latestRawPayload.asStateFlow()

    init {
        val dao = AppDatabase.getDatabase(application).sensorDataDao()
        repository = SensorRepository(dao)

        // Observe incoming BLE data stream and record to database
        viewModelScope.launch {
            bleManager.receivedDataStream.collect { (valNum, rawPayload) ->
                _latestReading.value = valNum
                _latestRawPayload.value = rawPayload

                try {
                    if (rawPayload.contains("NO FINGER")) {
                        _realSpO2.value = null
                        _realHeartRate.value = null
                    } else if (rawPayload.contains("SpO2:") && rawPayload.contains("BPM:")) {
                        val spo2Match = Regex("""SpO2:\s*([\d.]+)""").find(rawPayload)
                        val bpmMatch = Regex("""BPM:\s*([\d.]+)""").find(rawPayload)
                        if (spo2Match != null) {
                            _realSpO2.value = spo2Match.groupValues[1].toFloatOrNull()
                        }
                        if (bpmMatch != null) {
                            _realHeartRate.value = bpmMatch.groupValues[1].toFloatOrNull()
                        }
                    } else if (valNum > 0f) {
                        _realHeartRate.value = valNum
                    }
                } catch (e: Exception) {
                    // Ignore parsing error
                }

                val deviceName = (connectionState.value as? BleConnectionState.Connected)?.deviceName ?: "Wearable"
                repository.saveReading(valNum, rawPayload, deviceName)
                refreshTrendData()
            }
        }

        // Collect DB updates
        viewModelScope.launch {
            repository.recentReadings.collect {
                recentReadings.value = it
            }
        }
        viewModelScope.launch {
            repository.totalCount.collect {
                totalCount.value = it
            }
        }

        refreshTrendData()
    }

    fun startScan() {
        bleManager.startScan()
    }

    fun stopScan() {
        bleManager.stopScan()
    }

    fun connectToDevice(address: String) {
        bleManager.connectToDevice(address)
    }

    fun disconnect() {
        bleManager.disconnect()
    }

    fun setTimeFrame(days: Int) {
        _selectedTimeFrame.value = days
        refreshTrendData()
    }

    fun refreshTrendData() {
        viewModelScope.launch {
            _trendData.value = repository.getBatchTrendData(_selectedTimeFrame.value)
        }
    }

    fun clearDataHistory() {
        viewModelScope.launch {
            repository.clearHistory()
            _latestReading.value = null
            refreshTrendData()
        }
    }

    fun exportDataAsCsv(onResult: (String) -> Unit) {
        viewModelScope.launch {
            val csvContent = repository.exportDataAsCsv()
            onResult(csvContent)
        }
    }

    fun exportDataAsJson(onResult: (String) -> Unit) {
        viewModelScope.launch {
            val jsonContent = repository.exportDataAsJson()
            onResult(jsonContent)
        }
    }

    val emergencyDispatcher = bleManager.emergencyDispatcher
    val emergencyContactManager = com.example.blewearable.data.EmergencyContactManager(application)

    val isEmergencyActive: StateFlow<Boolean> = emergencyDispatcher.isEmergencyActive
    val lastEmergencyLog: StateFlow<String?> = emergencyDispatcher.lastEmergencyLog

    // App Theme State (Light vs Dark mode toggle)
    private val _isDarkMode = MutableStateFlow(false)
    val isDarkMode: StateFlow<Boolean> = _isDarkMode.asStateFlow()

    fun toggleDarkMode(enabled: Boolean) {
        _isDarkMode.value = enabled
    }

    fun toggleSoundAlert(enabled: Boolean) {
        emergencyDispatcher.enableSoundAlert = enabled
    }

    fun isSoundAlertEnabled(): Boolean = emergencyDispatcher.enableSoundAlert

    fun saveEmergencyContacts(primary: String, secondary: String, message: String) {
        emergencyContactManager.primaryContact = primary
        emergencyContactManager.secondaryContact = secondary
        emergencyContactManager.customSosMessage = message
    }

    fun triggerTestEmergency() {
        emergencyDispatcher.triggerEmergency("Manual Test SOS Triggered from App")
    }

    fun stopEmergencyAlert() {
        emergencyDispatcher.stopEmergencyAlert()
    }

    // Helper method to insert 30 days of mock simulated PPG health readings
    fun generateMockBatchData() {
        viewModelScope.launch {
            val now = System.currentTimeMillis()
            val dayMs = 24 * 60 * 60 * 1000L
            val device = "Wearable Health Monitor"

            for (i in 0..29) {
                val timestamp = now - (i * dayMs)
                val baseValue = 72f + Random.nextInt(-6, 8)
                for (j in 1..3) {
                    val valNum = baseValue + Random.nextInt(-3, 4)
                    repository.saveReading(valNum, "PPG_MOCK", device)
                }
            }
            _selectedTimeFrame.value = 30
            refreshTrendData()
        }
    }
}
