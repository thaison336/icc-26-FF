package com.example.blewearable.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import com.example.blewearable.MainActivity
import com.example.blewearable.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class BleManager(private val context: Context) {

    companion object {
        const val BATTERY_ALERT_CHANNEL_ID = "ble_battery_alert_channel"
        const val BATTERY_NOTIFICATION_ID = 9002
    }

    val emergencyDispatcher = EmergencyDispatcher(context)

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter

    private val _connectionState = MutableStateFlow<BleConnectionState>(BleConnectionState.Disconnected)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

    private val _topFsmState = MutableStateFlow<SomniGuardTopFsmState>(SomniGuardTopFsmState.INACTIVE)
    val topFsmState: StateFlow<SomniGuardTopFsmState> = _topFsmState.asStateFlow()

    // Trạng thái pin kit SomniGuard (đo qua ADC và truyền qua BLE)
    private val _batteryLevel = MutableStateFlow<Int?>(null)
    val batteryLevel: StateFlow<Int?> = _batteryLevel.asStateFlow()

    private val _batteryVoltageMv = MutableStateFlow<Int?>(null)
    val batteryVoltageMv: StateFlow<Int?> = _batteryVoltageMv.asStateFlow()

    private val _isBatteryLow = MutableStateFlow<Boolean>(false)
    val isBatteryLow: StateFlow<Boolean> = _isBatteryLow.asStateFlow()

    private var lastLowBatteryNotificationTime = 0L

    fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    fun getEnableBluetoothIntent(): Intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)

    private val _scannedDevices = MutableStateFlow<List<BleDeviceModel>>(emptyList())
    val scannedDevices: StateFlow<List<BleDeviceModel>> = _scannedDevices.asStateFlow()

    private val _receivedDataStream = MutableSharedFlow<Pair<Float, String>>(extraBufferCapacity = 64)
    val receivedDataStream: SharedFlow<Pair<Float, String>> = _receivedDataStream.asSharedFlow()

    private var activeGatt: BluetoothGatt? = null
    private var isScanning = false

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = device.name

            // Filter out unnamed or 'Unknown' devices so users only see named wearables
            if (name.isNullOrBlank() || 
                name.equals("Unknown Device", ignoreCase = true) || 
                name.equals("Unknown Wearable", ignoreCase = true)) {
                return
            }

            val address = device.address
            val rssi = result.rssi

            val currentList = _scannedDevices.value.toMutableList()
            val index = currentList.indexOfFirst { it.address == address }

            val updatedModel = BleDeviceModel(name = name, address = address, rssi = rssi)

            if (index >= 0) {
                currentList[index] = updatedModel
            } else {
                currentList.add(updatedModel)
            }
            _scannedDevices.value = currentList.sortedByDescending { it.rssi }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e("BleManager", "Scan failed with error code: $errorCode")
            _connectionState.value = BleConnectionState.Error("Scan failed code: $errorCode")
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
            _connectionState.value = BleConnectionState.Error("Bluetooth is turned off")
            return
        }
        val scanner = bluetoothAdapter.bluetoothLeScanner ?: return
        _scannedDevices.value = emptyList()
        _connectionState.value = BleConnectionState.Scanning
        isScanning = true
        scanner.startScan(scanCallback)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (isScanning) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            isScanning = false
            if (_connectionState.value is BleConnectionState.Scanning) {
                _connectionState.value = BleConnectionState.Disconnected
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun connectToDevice(address: String) {
        stopScan()
        val device = bluetoothAdapter?.getRemoteDevice(address) ?: run {
            _connectionState.value = BleConnectionState.Error("Device not found")
            return
        }

        val deviceName = device.name ?: address
        _connectionState.value = BleConnectionState.Connecting(deviceName)

        activeGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, gattCallback)
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        activeGatt?.apply {
            disconnect()
            close()
        }
        activeGatt = null
        _connectionState.value = BleConnectionState.Disconnected
        _topFsmState.value = SomniGuardTopFsmState.INACTIVE
        _batteryLevel.value = null
        _batteryVoltageMv.value = null
        _isBatteryLow.value = false
    }

    private fun createBatteryNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                BATTERY_ALERT_CHANNEL_ID,
                "Wearable Battery Alert",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Alert when SomniGuard wearable battery is low"
                enableVibration(true)
            }
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager
            manager?.createNotificationChannel(channel)
        }
    }

    @SuppressLint("MissingPermission")
    fun showLowBatteryNotification(percent: Int, voltageMv: Int? = null, force: Boolean = false) {
        val now = System.currentTimeMillis()
        // 5-minute cooldown between push notifications if battery remains low
        if (!force && (now - lastLowBatteryNotificationTime < 5 * 60 * 1000L)) {
            return
        }
        lastLowBatteryNotificationTime = now

        createBatteryNotificationChannel()

        val pendingIntent = PendingIntent.getActivity(
            context,
            1,
            Intent(context, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val voltageStr = if (voltageMv != null && voltageMv > 0) " (${String.format("%.2f", voltageMv / 1000f)}V)" else ""
        val notification = NotificationCompat.Builder(context, BATTERY_ALERT_CHANNEL_ID)
            .setContentTitle("⚠️ SomniGuard Battery Low ($percent%)")
            .setContentText("SomniGuard battery is at $percent%$voltageStr. Please charge your device to maintain monitoring.")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentIntent(pendingIntent)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .build()

        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager
        manager?.notify(BATTERY_NOTIFICATION_ID, notification)
    }

    fun dismissLowBatteryAlert() {
        _isBatteryLow.value = false
    }

    fun setSimulatedBattery(percent: Int, voltageMv: Int) {
        _batteryLevel.value = percent
        _batteryVoltageMv.value = voltageMv
        if (percent <= 20) {
            _isBatteryLow.value = true
            showLowBatteryNotification(percent, voltageMv, force = true)
        } else {
            _isBatteryLow.value = false
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val deviceName = gatt.device.name ?: gatt.device.address
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d("BleManager", "GATT Connected. Discovering services...")
                _connectionState.value = BleConnectionState.Connecting("$deviceName (Discovering services)")
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.d("BleManager", "GATT Disconnected")
                _connectionState.value = BleConnectionState.Disconnected
                _topFsmState.value = SomniGuardTopFsmState.INACTIVE
                _batteryLevel.value = null
                _batteryVoltageMv.value = null
                _isBatteryLow.value = false
                gatt.close()
                activeGatt = null
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val deviceName = gatt.device.name ?: gatt.device.address
                _connectionState.value = BleConnectionState.Connected(deviceName, gatt.device.address)

                // Locate target service & characteristic
                var targetChar: BluetoothGattCharacteristic? = null
                val service = gatt.getService(BleConstants.CUSTOM_SERVICE_UUID)
                if (service != null) {
                    targetChar = service.getCharacteristic(BleConstants.CUSTOM_CHARACTERISTIC_UUID)
                }

                // If specific UUID not found, fallback to first notify characteristic discovered
                if (targetChar == null) {
                    for (s in gatt.services) {
                        for (c in s.characteristics) {
                            if ((c.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY) != 0 ||
                                (c.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE) != 0) {
                                targetChar = c
                                break
                            }
                        }
                        if (targetChar != null) break
                    }
                }

                targetChar?.let { characteristic ->
                    Log.d("BleManager", "Enabling notification for characteristic: ${characteristic.uuid}")
                    gatt.setCharacteristicNotification(characteristic, true)

                    var descriptor = characteristic.getDescriptor(BleConstants.CCCD_UUID)
                    if (descriptor == null) {
                        descriptor = characteristic.descriptors.firstOrNull {
                            it.uuid.toString().equals(BleConstants.CCCD_UUID.toString(), ignoreCase = true) ||
                            it.uuid.toString().startsWith("00002902")
                        } ?: characteristic.descriptors.firstOrNull()
                    }

                    descriptor?.let { desc ->
                        Log.d("BleManager", "Writing CCCD descriptor: ${desc.uuid}")
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            gatt.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                        } else {
                            @Suppress("DEPRECATION")
                            desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                            @Suppress("DEPRECATION")
                            gatt.writeDescriptor(desc)
                        }
                    }
                }
            } else {
                _connectionState.value = BleConnectionState.Error("Service discovery failed: $status")
            }
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val bytes = characteristic.value ?: byteArrayOf()
            processIncomingBytes(bytes)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            processIncomingBytes(value)
        }
    }

    private fun processIncomingBytes(bytes: ByteArray) {
        if (bytes.isEmpty()) return
        val rawStr = bytes.joinToString(" ") { String.format("%02X", it) }
        var asciiStr = String(bytes).trim()

        var numericValue: Float = 0f

        // 1. Gói Telemetry 12 bytes nhị phân từ SomniGuard xG26 DevKit:
        // [0..1]: seq_num, [2..3]: spo2_x100, [4..5]: hr_x10, [6..7]: motion_mg, [8]: posture_flags, [9]: top_fsm, [10]: sub_fsm, [11]: bat
        if (bytes.size == 12) {
            val spo2Raw = (bytes[2].toInt() and 0xFF) or ((bytes[3].toInt() and 0xFF) shl 8)
            val hrRaw   = (bytes[4].toInt() and 0xFF) or ((bytes[5].toInt() and 0xFF) shl 8)
            val postureFlags = bytes[8].toInt() and 0xFF
            val isFingerAttached = (postureFlags and (1 shl 3)) != 0
            val topFsmCode = bytes[9].toInt() and 0xFF
            val battPercent = bytes[11].toInt() and 0xFF

            _topFsmState.value = SomniGuardTopFsmState.fromCode(topFsmCode)
            _batteryLevel.value = battPercent

            if (battPercent in 1..20) {
                _isBatteryLow.value = true
                showLowBatteryNotification(battPercent, _batteryVoltageMv.value)
            } else if (battPercent > 20) {
                _isBatteryLow.value = false
            }

            if (isFingerAttached) {
                val spo2Val = spo2Raw / 100.0f
                val hrVal   = hrRaw / 10.0f
                numericValue = hrVal
                asciiStr = String.format("SpO2:%.1f%% BPM:%.0f", spo2Val, hrVal)
            } else {
                numericValue = 0f
                asciiStr = "NO FINGER"
            }
        } 
        // 2. Gói Event 8 bytes nhị phân (somniguard_ble_event_pkt_t):
        // [0]: type, [1]: code, [2..3]: seq_num, [4..5]: param1, [6..7]: param2
        else if (bytes.size == 8) {
            val eventType = bytes[0].toInt() and 0xFF
            val eventCode = bytes[1].toInt() and 0xFF
            val param1 = (bytes[4].toInt() and 0xFF) or ((bytes[5].toInt() and 0xFF) shl 8)
            val param2 = (bytes[6].toInt() and 0xFF) or ((bytes[7].toInt() and 0xFF) shl 8)

            when (eventCode) {
                0x31 -> { // SOMNIGUARD_BLE_EVT_CODE_BATTERY_LOW
                    val battPercent = param1
                    val battVoltageMv = param2
                    _batteryLevel.value = battPercent
                    _batteryVoltageMv.value = battVoltageMv
                    _isBatteryLow.value = true
                    asciiStr = "LOW BATT: $battPercent% (${battVoltageMv}mV)"
                    showLowBatteryNotification(battPercent, battVoltageMv, force = true)
                }
                0x33 -> { // SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG
                    _topFsmState.value = SomniGuardTopFsmState.fromCode(param2)
                    asciiStr = "FSM CHG -> ${_topFsmState.value.stateName}"
                }
                0x21 -> { // SOMNIGUARD_BLE_EVT_CODE_FINGER_REMOVED
                    _topFsmState.value = SomniGuardTopFsmState.OFF_FINGER_SUSPEND
                    asciiStr = "NO FINGER"
                }
                0x22 -> { // SOMNIGUARD_BLE_EVT_CODE_FINGER_ATTACHED
                    _topFsmState.value = SomniGuardTopFsmState.ACTIVE_MODE
                    asciiStr = "FINGER ATTACHED"
                }
                0x11, 0x12 -> { // SOMNIGUARD_BLE_EVT_CODE_APNEA_WARNING (Gửi từ somniguard_BLE_control())
                    _topFsmState.value = SomniGuardTopFsmState.DEEP_ANALYSIS
                    asciiStr = "SOS" // Đánh dấu payload SOS chính thức
                }
            }
        } else {
            numericValue = asciiStr.toFloatOrNull() ?: 0f

            // Suy luận FSM State từ chuỗi ASCII (nếu firmware gửi dạng string):
            if (asciiStr.contains("NO FINGER", ignoreCase = true)) {
                _topFsmState.value = SomniGuardTopFsmState.OFF_FINGER_SUSPEND
            } else if (asciiStr.startsWith("SpO2:", ignoreCase = true)) {
                if (_topFsmState.value == SomniGuardTopFsmState.INACTIVE || _topFsmState.value == SomniGuardTopFsmState.OFF_FINGER_SUSPEND) {
                    _topFsmState.value = SomniGuardTopFsmState.NORMAL_SLEEP
                }
            }
        }

        // CHỈ ALARM khi nhận được đúng gói tin SOS phát từ somniguard_BLE_control() hoặc chuỗi "SOS":
        // (Nếu chỉ đơn thuần chuyển sang DEEP ANALYSIS trong luồng Telemetry thì KHÔNG bật alarm)
        val isEmergency = asciiStr.equals("SOS", ignoreCase = true) ||
                asciiStr.startsWith("SOS", ignoreCase = true) ||
                asciiStr.contains("EMERGENCY", ignoreCase = true) ||
                asciiStr.contains("HELP", ignoreCase = true) ||
                numericValue == -999f

        if (!isEmergency) {
            // Khi thiết bị quay lại trạng thái an toàn / bình thường, mở lại cờ cho các sự cố tiếp theo
            emergencyDispatcher.resetDismissedState()
        } else {
            // Chỉ kích hoạt chuông nếu chưa có alarm đang kêu VÀ người dùng chưa bấm nút dừng trong phiên SOS này
            if (!emergencyDispatcher.isEmergencyActive.value && !emergencyDispatcher.isDismissedByUser) {
                Log.w("BleManager", "🚨 Explicit Wearable SOS Received (somniguard_BLE_control) -> Triggering Phone Alarm!")
                emergencyDispatcher.triggerEmergency("SomniGuard Wearable SOS Alert ($asciiStr)")
            }
        }

        CoroutineScope(Dispatchers.IO).launch {
            _receivedDataStream.emit(Pair(numericValue, "$asciiStr [$rawStr]"))
        }
    }
}
