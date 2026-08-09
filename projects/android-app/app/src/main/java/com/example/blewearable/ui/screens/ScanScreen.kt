package com.example.blewearable.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Watch
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ui.components.DeviceItem
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.StatusGreen
import com.example.blewearable.ui.theme.StatusOrange
import com.example.blewearable.ui.theme.StatusRed
import com.example.blewearable.viewmodel.MainViewModel

@Composable
fun ScanScreen(viewModel: MainViewModel) {
    val connectionState by viewModel.connectionState.collectAsState()
    val scannedDevices by viewModel.scannedDevices.collectAsState()

    val isScanning = connectionState is BleConnectionState.Scanning
    val isConnected = connectionState is BleConnectionState.Connected
    val connectedDeviceName = (connectionState as? BleConnectionState.Connected)?.deviceName ?: ""
    val isBtEnabled = viewModel.bleManager.isBluetoothEnabled()

    var showDisconnectDialog by remember { mutableStateOf(false) }

    val enableBtLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (viewModel.bleManager.isBluetoothEnabled()) {
            viewModel.startScan()
        }
    }

    // Disconnect Confirmation Dialog
    if (showDisconnectDialog) {
        AlertDialog(
            onDismissRequest = { showDisconnectDialog = false },
            icon = {
                Icon(
                    imageVector = Icons.Default.Watch,
                    contentDescription = "Disconnect Warning",
                    tint = StatusRed,
                    modifier = Modifier.size(36.dp)
                )
            },
            title = {
                Text(
                    text = "Disconnect Wearable?",
                    style = MaterialTheme.typography.titleLarge
                )
            },
            text = {
                Text(
                    text = "Are you sure you want to disconnect from \"$connectedDeviceName\"?\n\nAutomatic safety monitoring will pause while disconnected.",
                    style = MaterialTheme.typography.bodyLarge
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        showDisconnectDialog = false
                        viewModel.disconnect()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = StatusRed),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Text("Disconnect", color = Color.White)
                }
            },
            dismissButton = {
                OutlinedButton(
                    onClick = { showDisconnectDialog = false },
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Text("Cancel")
                }
            },
            shape = RoundedCornerShape(20.dp),
            containerColor = MaterialTheme.colorScheme.surface
        )
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(16.dp)
    ) {
        // Page Title & Scan Action Header
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "Wearable Connection",
                    style = MaterialTheme.typography.headlineLarge,
                    color = MaterialTheme.colorScheme.onBackground
                )
                Text(
                    text = if (isConnected) "Connected & Monitoring Active" else "Pair your watch or safety button",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
                )
            }

            Spacer(modifier = Modifier.width(12.dp))

            if (!isConnected) {
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
                    shape = RoundedCornerShape(14.dp),
                    modifier = Modifier.height(50.dp)
                ) {
                    Icon(
                        imageVector = if (isScanning) Icons.Default.Stop else Icons.Default.Refresh,
                        contentDescription = "Scan Toggle"
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = if (isScanning) "Stop" else "Scan",
                        style = MaterialTheme.typography.titleMedium,
                        color = Color.White
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(20.dp))

        // Prominent Connected Wearable Card
        if (isConnected) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                shape = RoundedCornerShape(20.dp)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(2.dp, StatusGreen, RoundedCornerShape(20.dp))
                        .padding(20.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Box(
                                modifier = Modifier
                                    .size(48.dp)
                                    .clip(CircleShape)
                                    .background(StatusGreen.copy(alpha = 0.15f)),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    imageVector = Icons.Default.Watch,
                                    contentDescription = "Connected",
                                    tint = StatusGreen,
                                    modifier = Modifier.size(28.dp)
                                )
                            }
                            Spacer(modifier = Modifier.width(14.dp))
                            Column {
                                Text(
                                    text = connectedDeviceName,
                                    style = MaterialTheme.typography.titleLarge,
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    Icon(
                                        imageVector = Icons.Default.CheckCircle,
                                        contentDescription = "Status",
                                        tint = StatusGreen,
                                        modifier = Modifier.size(16.dp)
                                    )
                                    Spacer(modifier = Modifier.width(4.dp))
                                    Text(
                                        text = "CONNECTED & ACTIVE",
                                        style = MaterialTheme.typography.labelLarge,
                                        color = StatusGreen
                                    )
                                }
                            }
                        }

                        Button(
                            onClick = { showDisconnectDialog = true },
                            colors = ButtonDefaults.buttonColors(containerColor = StatusRed.copy(alpha = 0.15f)),
                            shape = RoundedCornerShape(12.dp)
                        ) {
                            Text("Disconnect", color = StatusRed, style = MaterialTheme.typography.labelLarge)
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(20.dp))
        }

        // Bluetooth OFF Warning Button
        if (!isBtEnabled) {
            Button(
                onClick = { enableBtLauncher.launch(viewModel.bleManager.getEnableBluetoothIntent()) },
                colors = ButtonDefaults.buttonColors(containerColor = StatusOrange),
                shape = RoundedCornerShape(14.dp),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(54.dp)
            ) {
                Icon(Icons.Default.Bluetooth, contentDescription = "Enable Bluetooth", tint = Color.White)
                Spacer(modifier = Modifier.width(10.dp))
                Text(
                    text = "Bluetooth is OFF. Tap to Turn ON",
                    style = MaterialTheme.typography.titleMedium,
                    color = Color.White
                )
            }
            Spacer(modifier = Modifier.height(16.dp))
        }

        // Scanning Active Banner
        if (isScanning) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                CircularProgressIndicator(
                    modifier = Modifier.size(24.dp),
                    color = PrimaryBlue,
                    strokeWidth = 3.dp
                )
                Spacer(modifier = Modifier.width(12.dp))
                Text(
                    text = "Scanning for nearby safety wearables...",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onBackground
                )
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        // List of Scanned Wearable Devices (Filtered to Named Devices Only)
        if (!isConnected) {
            if (scannedDevices.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(
                            imageVector = Icons.Default.Watch,
                            contentDescription = "No Devices",
                            tint = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.3f),
                            modifier = Modifier.size(64.dp)
                        )
                        Spacer(modifier = Modifier.height(12.dp))
                        Text(
                            text = if (isScanning) "Searching for your wearable device..." else "No wearable watches found.\nTap 'Scan' above to search.",
                            style = MaterialTheme.typography.bodyLarge,
                            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f)
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
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
}
