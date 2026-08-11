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
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
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

    val emergencyDispatcher = EmergencyDispatcher(context)

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter

    private val _connectionState = MutableStateFlow<BleConnectionState>(BleConnectionState.Disconnected)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

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
        val asciiStr = String(bytes).trim()

        // Parse numerical value from string or direct payload
        val numericValue: Float = asciiStr.toFloatOrNull()
            ?: try {
                if (bytes.size >= 4) {
                    java.nio.ByteBuffer.wrap(bytes).float
                } else if (bytes.size == 2) {
                    ((bytes[0].toInt() and 0xFF) or ((bytes[1].toInt() and 0xFF) shl 8)).toFloat()
                } else {
                    (bytes[0].toInt() and 0xFF).toFloat()
                }
            } catch (e: Exception) {
                0f
            }

        // Check if payload represents emergency trigger
        val isEmergency = asciiStr.contains("SOS", ignoreCase = true) ||
                asciiStr.contains("EMERGENCY", ignoreCase = true) ||
                asciiStr.contains("HELP", ignoreCase = true) ||
                bytes.any { (it.toInt() and 0xFF) == 0xFF } ||
                numericValue == -999f

        if (isEmergency) {
            Log.w("BleManager", "Emergency payload detected in BLE stream!")
            emergencyDispatcher.triggerEmergency("Wearable SOS Button Pressed ($asciiStr)")
        }

        CoroutineScope(Dispatchers.IO).launch {
            _receivedDataStream.emit(Pair(numericValue, "$asciiStr [$rawStr]"))
        }
    }
}
