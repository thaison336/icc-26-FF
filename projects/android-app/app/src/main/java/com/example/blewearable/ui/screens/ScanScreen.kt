package com.example.blewearable.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ui.components.DeviceItem
import com.example.blewearable.ui.theme.DarkBackground
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.StatusOrange
import com.example.blewearable.ui.theme.StatusRed
import com.example.blewearable.ui.theme.TextSecondary
import com.example.blewearable.viewmodel.MainViewModel

@Composable
fun ScanScreen(viewModel: MainViewModel) {
    val connectionState by viewModel.connectionState.collectAsState()
    val scannedDevices by viewModel.scannedDevices.collectAsState()

    val isScanning = connectionState is BleConnectionState.Scanning
    val isBtEnabled = viewModel.bleManager.isBluetoothEnabled()

    val enableBtLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (viewModel.bleManager.isBluetoothEnabled()) {
            viewModel.startScan()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(DarkBackground)
            .padding(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "BLE Wearables",
                    style = MaterialTheme.typography.headlineMedium
                )
                Text(
                    text = "Tap a device to connect",
                    style = MaterialTheme.typography.bodyMedium
                )
            }

            Button(
                onClick = {
                    if (!viewModel.bleManager.isBluetoothEnabled()) {
                        enableBtLauncher.launch(viewModel.bleManager.getEnableBluetoothIntent())
                    } else if (isScanning) {
                        viewModel.stopScan()
                    } else {
                        viewModel.startScan()
                    }
                },
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isScanning) StatusRed else PrimaryBlue
                ),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(
                    imageVector = if (isScanning) Icons.Default.Stop else Icons.Default.Refresh,
                    contentDescription = "Scan toggle"
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(text = if (isScanning) "Stop" else "Scan")
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        if (!isBtEnabled) {
            Button(
                onClick = { enableBtLauncher.launch(viewModel.bleManager.getEnableBluetoothIntent()) },
                colors = ButtonDefaults.buttonColors(containerColor = StatusOrange),
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Default.Bluetooth, contentDescription = "Enable Bluetooth")
                Spacer(modifier = Modifier.width(8.dp))
                Text("Bluetooth is OFF. Tap to Turn ON")
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        if (isScanning) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                CircularProgressIndicator(
                    modifier = Modifier.width(20.dp),
                    color = PrimaryBlue,
                    strokeWidth = 2.dp
                )
                Spacer(modifier = Modifier.width(12.dp))
                Text(
                    text = "Scanning for nearby BLE devices...",
                    style = MaterialTheme.typography.labelMedium
                )
            }
        }

        if (scannedDevices.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = if (isScanning) "Searching..." else "No BLE devices found. Tap Scan.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = TextSecondary
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(scannedDevices) { device ->
                    DeviceItem(
                        device = device,
                        onConnect = { viewModel.connectToDevice(it) }
                    )
                }
            }
        }
    }
}
