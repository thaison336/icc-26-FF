package com.example.blewearable.ui.screens

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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoGraph
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.PhoneInTalk
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
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
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ui.components.HistoricalTrendChart
import com.example.blewearable.ui.theme.AccentTeal
import com.example.blewearable.ui.theme.DarkBackground
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.StatusGreen
import com.example.blewearable.ui.theme.StatusOrange
import com.example.blewearable.ui.theme.StatusRed
import com.example.blewearable.ui.theme.SurfaceCard
import com.example.blewearable.ui.theme.SurfaceCardBorder
import com.example.blewearable.ui.theme.TextPrimary
import com.example.blewearable.ui.theme.TextSecondary
import com.example.blewearable.viewmodel.MainViewModel

@Composable
fun DashboardScreen(viewModel: MainViewModel) {
    val connectionState by viewModel.connectionState.collectAsState()
    val trendData by viewModel.trendData.collectAsState()
    val selectedTimeFrame by viewModel.selectedTimeFrame.collectAsState()
    val latestReading by viewModel.latestReading.collectAsState()
    val totalCount by viewModel.totalCount.collectAsState()
    val isEmergencyActive by viewModel.isEmergencyActive.collectAsState()
    val lastEmergencyLog by viewModel.lastEmergencyLog.collectAsState()

    var primaryContactInput by remember {
        mutableStateOf(viewModel.emergencyContactManager.primaryContact)
    }
    var secondaryContactInput by remember {
        mutableStateOf(viewModel.emergencyContactManager.secondaryContact)
    }
    var sosMessageInput by remember {
        mutableStateOf(viewModel.emergencyContactManager.customSosMessage)
    }
    var isSavedNoticeVisible by remember { mutableStateOf(false) }

    val scrollState = rememberScrollState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(DarkBackground)
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        // App Title
        Text(
            text = "Wearable Safety & Health Dashboard",
            style = MaterialTheme.typography.headlineMedium
        )
        Spacer(modifier = Modifier.height(16.dp))

        // Active Emergency Alert Banner (Ringing / Vibrating / Auto-Call Active)
        if (isEmergencyActive) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = StatusRed.copy(alpha = 0.15f)),
                shape = RoundedCornerShape(16.dp)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(2.dp, StatusRed, RoundedCornerShape(16.dp))
                        .padding(16.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.NotificationsActive,
                            contentDescription = "Active Emergency",
                            tint = StatusRed,
                            modifier = Modifier.padding(end = 8.dp)
                        )
                        Text(
                            text = "🚨 EMERGENCY ACTIVE - RINGING & CALLING",
                            style = MaterialTheme.typography.titleMedium,
                            color = StatusRed
                        )
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Wearable triggered emergency alert! Phone alarm is ringing, vibration active, SMS dispatched, and auto SOS call initiated.",
                        style = MaterialTheme.typography.bodySmall,
                        color = TextPrimary
                    )
                    Spacer(modifier = Modifier.height(12.dp))
                    Button(
                        onClick = { viewModel.stopEmergencyAlert() },
                        colors = ButtonDefaults.buttonColors(containerColor = StatusRed),
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("STOP ALARM & VIBRATION", color = Color.White)
                    }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }

        // Connection Status Banner
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = SurfaceCard),
            shape = RoundedCornerShape(16.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, SurfaceCardBorder, RoundedCornerShape(16.dp))
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    val (statusText, statusColor) = when (connectionState) {
                        is BleConnectionState.Connected -> "Connected" to StatusGreen
                        is BleConnectionState.Connecting -> "Connecting..." to StatusOrange
                        is BleConnectionState.Scanning -> "Scanning..." to PrimaryBlue
                        else -> "Disconnected" to StatusRed
                    }

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(statusColor.copy(alpha = 0.2f))
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = statusText,
                            color = statusColor,
                            style = MaterialTheme.typography.labelMedium
                        )
                    }

                    Spacer(modifier = Modifier.width(12.dp))

                    if (connectionState is BleConnectionState.Connected) {
                        Text(
                            text = (connectionState as BleConnectionState.Connected).deviceName,
                            style = MaterialTheme.typography.titleLarge
                        )
                    }
                }

                if (connectionState is BleConnectionState.Connected) {
                    OutlinedButton(
                        onClick = { viewModel.disconnect() },
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text("Disconnect", color = StatusRed)
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Emergency Contacts & SOS Settings Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = SurfaceCard),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, SurfaceCardBorder, RoundedCornerShape(16.dp))
                    .padding(20.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Call,
                        contentDescription = "Emergency Setup",
                        tint = StatusOrange
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Emergency SOS Setup (Auto Call & SMS)",
                        style = MaterialTheme.typography.titleMedium,
                        color = TextPrimary
                    )
                }
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "When the wearable watch receives an emergency signal, your phone will ring/vibrate loudly, send SMS to all contacts below, and automatically dial the Primary Contact.",
                    style = MaterialTheme.typography.bodySmall,
                    color = TextSecondary
                )
                Spacer(modifier = Modifier.height(16.dp))

                // Primary Contact Input
                OutlinedTextField(
                    value = primaryContactInput,
                    onValueChange = { primaryContactInput = it },
                    label = { Text("Primary Contact Number (Auto-Call & SMS)") },
                    placeholder = { Text("e.g. +1234567890") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = PrimaryBlue,
                        unfocusedBorderColor = SurfaceCardBorder,
                        focusedLabelColor = PrimaryBlue,
                        unfocusedLabelColor = TextSecondary,
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(12.dp))

                // Secondary Contact Input
                OutlinedTextField(
                    value = secondaryContactInput,
                    onValueChange = { secondaryContactInput = it },
                    label = { Text("Secondary Contact Number (SMS)") },
                    placeholder = { Text("e.g. +1987654321") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Phone),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = PrimaryBlue,
                        unfocusedBorderColor = SurfaceCardBorder,
                        focusedLabelColor = PrimaryBlue,
                        unfocusedLabelColor = TextSecondary,
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(12.dp))

                // Custom SOS Message
                OutlinedTextField(
                    value = sosMessageInput,
                    onValueChange = { sosMessageInput = it },
                    label = { Text("Custom Emergency SMS Message") },
                    modifier = Modifier.fillMaxWidth(),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = PrimaryBlue,
                        unfocusedBorderColor = SurfaceCardBorder,
                        focusedLabelColor = PrimaryBlue,
                        unfocusedLabelColor = TextSecondary,
                        focusedTextColor = TextPrimary,
                        unfocusedTextColor = TextPrimary
                    )
                )

                Spacer(modifier = Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Button(
                        onClick = {
                            viewModel.saveEmergencyContacts(
                                primaryContactInput,
                                secondaryContactInput,
                                sosMessageInput
                            )
                            isSavedNoticeVisible = true
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.Save, contentDescription = "Save")
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Save Contacts")
                    }

                    Button(
                        onClick = { viewModel.triggerTestEmergency() },
                        colors = ButtonDefaults.buttonColors(containerColor = StatusOrange),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.Warning, contentDescription = "Test SOS")
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("TEST SOS TRIGGER")
                    }
                }

                if (isSavedNoticeVisible) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "✓ Emergency contacts saved successfully!",
                        color = StatusGreen,
                        style = MaterialTheme.typography.bodySmall
                    )
                }

                lastEmergencyLog?.let { log ->
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Last Log: $log",
                        color = TextSecondary,
                        style = MaterialTheme.typography.labelSmall
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Live Metric Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = SurfaceCard),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, SurfaceCardBorder, RoundedCornerShape(16.dp))
                    .padding(20.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Sensors,
                        contentDescription = "Sensor",
                        tint = AccentTeal
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Latest Sensor Value",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
                Spacer(modifier = Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        text = if (latestReading != null) String.format("%.1f", latestReading) else "--",
                        style = MaterialTheme.typography.headlineMedium.copy(fontSize = 36.sp),
                        color = TextPrimary
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "units",
                        style = MaterialTheme.typography.bodyMedium,
                        color = TextSecondary,
                        modifier = Modifier.padding(bottom = 6.dp)
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "Total DB records stored: $totalCount",
                    style = MaterialTheme.typography.labelMedium,
                    color = TextSecondary
                )
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        // Historical Trend Section Header & Time Filters
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "Historical Batch Trends",
                style = MaterialTheme.typography.titleLarge
            )

            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                listOf(1 to "24h", 7 to "7d", 30 to "30d").forEach { (days, label) ->
                    val isSelected = selectedTimeFrame == days
                    Button(
                        onClick = { viewModel.setTimeFrame(days) },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isSelected) PrimaryBlue else SurfaceCard
                        ),
                        shape = RoundedCornerShape(8.dp),
                        modifier = Modifier.height(32.dp)
                    ) {
                        Text(
                            text = label,
                            style = MaterialTheme.typography.labelMedium,
                            color = if (isSelected) TextPrimary else TextSecondary
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(12.dp))

        // Historical Chart Component
        HistoricalTrendChart(trendData = trendData)

        Spacer(modifier = Modifier.height(24.dp))

        // Quick Actions (Clear Data / Generate Demo Simulator Data)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Button(
                onClick = { viewModel.generateMockBatchData() },
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(Icons.Default.AutoGraph, contentDescription = "Simulate")
                Spacer(modifier = Modifier.width(6.dp))
                Text("Demo Simulator")
            }

            OutlinedButton(
                onClick = { viewModel.clearDataHistory() },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(12.dp)
            ) {
                Icon(Icons.Default.Delete, contentDescription = "Clear", tint = StatusRed)
                Spacer(modifier = Modifier.width(6.dp))
                Text("Clear History", color = StatusRed)
            }
        }
    }
}
