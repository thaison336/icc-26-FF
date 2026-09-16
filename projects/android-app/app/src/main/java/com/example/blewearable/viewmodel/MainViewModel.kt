package com.example.blewearable.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ble.BleDeviceModel
import com.example.blewearable.ble.BleManager
import com.example.blewearable.ble.SomniGuardTopFsmState
import com.example.blewearable.data.AppDatabase
import com.example.blewearable.data.BatchTrendSummary
import com.example.blewearable.data.EmergencyContactManager
import com.example.blewearable.data.LocationHelper
import com.example.blewearable.data.SensorDataEntity
import com.example.blewearable.data.SensorRepository
import com.example.blewearable.data.TimeRangeUnit
import com.example.blewearable.data.TrendTimeRange
import com.example.blewearable.data.UserLocationInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.random.Random

class MainViewModel(application: Application) : AndroidViewModel(application) {

    val bleManager = BleManager(application)
    val locationHelper = LocationHelper(application)
    val emergencyDispatcher = bleManager.emergencyDispatcher
    val emergencyContactManager = emergencyDispatcher.contactManager

    val isEmergencyActive: StateFlow<Boolean> = emergencyDispatcher.isEmergencyActive
    val lastEmergencyLog: StateFlow<String?> = emergencyDispatcher.lastEmergencyLog

    // Bedtime Location Caching State
    private val _cachedLocation = MutableStateFlow<UserLocationInfo?>(emergencyContactManager.getCachedLocationInfo())
    val cachedLocation: StateFlow<UserLocationInfo?> = _cachedLocation.asStateFlow()

    private val _isLocationUpdating = MutableStateFlow(false)
    val isLocationUpdating: StateFlow<Boolean> = _isLocationUpdating.asStateFlow()

    private val _locationUpdateStatus = MutableStateFlow<String?>(null)
    val locationUpdateStatus: StateFlow<String?> = _locationUpdateStatus.asStateFlow()

    // App Theme State (Light vs Dark mode toggle)
    private val _isDarkMode = MutableStateFlow(false)
    val isDarkMode: StateFlow<Boolean> = _isDarkMode.asStateFlow()

    private val repository: SensorRepository

    val connectionState: StateFlow<BleConnectionState> = bleManager.connectionState
    val scannedDevices: StateFlow<List<BleDeviceModel>> = bleManager.scannedDevices
    val topFsmState: StateFlow<SomniGuardTopFsmState> = bleManager.topFsmState

    val batteryLevel: StateFlow<Int?> = bleManager.batteryLevel
    val batteryVoltageMv: StateFlow<Int?> = bleManager.batteryVoltageMv
    val isBatteryLow: StateFlow<Boolean> = bleManager.isBatteryLow

    val recentReadings = MutableStateFlow<List<SensorDataEntity>>(emptyList())
    val totalCount = MutableStateFlow(0)

    private val _trendData = MutableStateFlow<List<BatchTrendSummary>>(emptyList())
    val trendData: StateFlow<List<BatchTrendSummary>> = _trendData.asStateFlow()

    private val _selectedTimeRange = MutableStateFlow<TrendTimeRange>(TrendTimeRange.RANGE_30_DAYS)
    val selectedTimeRange: StateFlow<TrendTimeRange> = _selectedTimeRange.asStateFlow()

    private val _selectedTimeFrame = MutableStateFlow(30) // Backward compatibility
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

        // Automatically fetch and cache phone's bedtime location when app opens
        fetchAndCacheCurrentLocation(silent = true)
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

    fun setTimeRange(timeRange: TrendTimeRange) {
        _selectedTimeRange.value = timeRange
        if (timeRange.unit == TimeRangeUnit.DAYS) {
            _selectedTimeFrame.value = timeRange.amount
        }
        refreshTrendData()
    }

    fun setCustomTimeRange(amount: Int, unit: TimeRangeUnit) {
        val shortLabel = when (unit) {
            TimeRangeUnit.MINUTES -> "${amount}m"
            TimeRangeUnit.HOURS -> "${amount}h"
            TimeRangeUnit.DAYS -> "${amount}d"
        }
        val customRange = TrendTimeRange(amount, unit, shortLabel)
        _selectedTimeRange.value = customRange
        refreshTrendData()
    }

    fun setTimeFrame(days: Int) {
        val matchingPreset = when (days) {
            1 -> TrendTimeRange.RANGE_24_HOURS
            7 -> TrendTimeRange.RANGE_7_DAYS
            30 -> TrendTimeRange.RANGE_30_DAYS
            else -> TrendTimeRange(days, TimeRangeUnit.DAYS, "${days}d")
        }
        setTimeRange(matchingPreset)
    }

    fun refreshTrendData() {
        viewModelScope.launch {
            _trendData.value = repository.getBatchTrendData(_selectedTimeRange.value.durationMs)
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

    fun fetchAndCacheCurrentLocation(silent: Boolean = false, onFinished: ((Boolean) -> Unit)? = null) {
        if (!locationHelper.hasLocationPermission()) {
            if (!silent) _locationUpdateStatus.value = "Location (GPS) permission is not granted."
            onFinished?.invoke(false)
            return
        }

        viewModelScope.launch {
            _isLocationUpdating.value = true
            try {
                val locInfo = locationHelper.fetchCurrentOrLastLocation()
                if (locInfo != null) {
                    emergencyContactManager.saveCachedLocation(locInfo)
                    _cachedLocation.value = locInfo
                    _locationUpdateStatus.value = "GPS location updated successfully."
                    onFinished?.invoke(true)
                } else {
                    if (!silent) _locationUpdateStatus.value = "Unable to obtain GPS fix. Please ensure Location is enabled."
                    onFinished?.invoke(false)
                }
            } catch (e: Exception) {
                if (!silent) _locationUpdateStatus.value = "Location error: ${e.message}"
                onFinished?.invoke(false)
            } finally {
                _isLocationUpdating.value = false
            }
        }
    }

    fun saveLocationSettings(manualAddress: String, locationMode: String) {
        emergencyContactManager.manualAddress = manualAddress
        emergencyContactManager.locationPreferenceMode = locationMode
    }

    fun getEmergencySmsPreview(): String {
        return emergencyDispatcher.buildEmergencySmsMessage("Manual Test SOS Alert")
    }

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
        emergencyDispatcher.triggerEmergency("Manual Test SOS Triggered from App", force = true)
    }

    fun stopEmergencyAlert() {
        emergencyDispatcher.stopEmergencyAlert()
    }

    fun dismissLowBatteryAlert() {
        bleManager.dismissLowBatteryAlert()
    }

    fun simulateBattery(percent: Int, voltageMv: Int) {
        bleManager.setSimulatedBattery(percent, voltageMv)
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
