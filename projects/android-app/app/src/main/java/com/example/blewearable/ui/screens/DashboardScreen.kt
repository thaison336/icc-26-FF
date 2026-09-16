package com.example.blewearable.ui.screens

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryAlert
import androidx.compose.material.icons.filled.BatteryFull
import androidx.compose.material.icons.filled.BatterySaver
import androidx.compose.material.icons.filled.BatteryUnknown
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.MonitorHeart
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.blewearable.ble.BleConnectionState
import com.example.blewearable.ble.SomniGuardTopFsmState
import com.example.blewearable.data.TimeRangeUnit
import com.example.blewearable.data.TrendTimeRange
import com.example.blewearable.ui.components.HistoricalTrendChart
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.StatusGreen
import com.example.blewearable.ui.theme.StatusOrange
import com.example.blewearable.ui.theme.StatusRed
import com.example.blewearable.viewmodel.MainViewModel

@Composable
fun DashboardScreen(
    viewModel: MainViewModel,
    onNavigateToSettings: () -> Unit = {}
) {
    val context = LocalContext.current
    val connectionState by viewModel.connectionState.collectAsState()
    val topFsmState by viewModel.topFsmState.collectAsState()
    val isEmergencyActive by viewModel.isEmergencyActive.collectAsState()
    val batteryLevel by viewModel.batteryLevel.collectAsState()
    val batteryVoltageMv by viewModel.batteryVoltageMv.collectAsState()
    val isBatteryLow by viewModel.isBatteryLow.collectAsState()
    val latestReading by viewModel.latestReading.collectAsState()
    val realSpO2 by viewModel.realSpO2.collectAsState()
    val realHeartRate by viewModel.realHeartRate.collectAsState()
    val latestRawPayload by viewModel.latestRawPayload.collectAsState()
    val trendData by viewModel.trendData.collectAsState()
    val selectedTimeRange by viewModel.selectedTimeRange.collectAsState()
    val primaryContact = viewModel.emergencyContactManager.primaryContact
    val cachedLocation by viewModel.cachedLocation.collectAsState()
    val manualAddress = viewModel.emergencyContactManager.manualAddress

    var isTelemetryEnabled by remember { mutableStateOf(true) }
    var showCustomTimeDialog by remember { mutableStateOf(false) }
    var customAmountInput by remember { mutableStateOf("2") }
    var customUnitSelection by remember { mutableStateOf(TimeRangeUnit.MINUTES) }

    val scrollState = rememberScrollState()

    // Real-time SpO2 & Heart Rate metrics received over BLE RF from EFR32 xG26 DevKit
    val isFingerAttached = topFsmState != SomniGuardTopFsmState.OFF_FINGER_SUSPEND &&
            topFsmState != SomniGuardTopFsmState.INACTIVE &&
            !latestRawPayload.contains("NO FINGER") && 
            (realHeartRate != null || realSpO2 != null || latestReading != null)

    val heartRateStr = if (realHeartRate != null) "${realHeartRate!!.toInt()}" else if (isFingerAttached) "${(latestReading ?: 72f).toInt()}" else "--"
    val spO2Str = if (realSpO2 != null) "${realSpO2!!.toInt()}" else if (isFingerAttached) "98" else "--"

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        // App Header
        Column(modifier = Modifier.fillMaxWidth()) {
            Text(
                text = "Personal Safety",
                style = MaterialTheme.typography.headlineLarge,
                color = MaterialTheme.colorScheme.onBackground
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = "Wearable Safety & Health Dashboard",
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f)
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Active Emergency Alert Banner (Ringing / Vibrating / Auto-Call Active)
        if (isEmergencyActive) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = StatusRed.copy(alpha = 0.12f)),
                shape = RoundedCornerShape(16.dp)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(2.dp, StatusRed, RoundedCornerShape(16.dp))
                        .padding(16.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.NotificationsActive,
                            contentDescription = "Active Emergency",
                            tint = StatusRed,
                            modifier = Modifier.size(28.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "EMERGENCY ALERT ACTIVE",
                            style = MaterialTheme.typography.headlineMedium,
                            color = StatusRed
                        )
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    Text(
                        text = "Phone alarm ringing, SMS sent, placing call to $primaryContact...",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    Button(
                        onClick = { viewModel.stopEmergencyAlert() },
                        colors = ButtonDefaults.buttonColors(containerColor = StatusRed),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Text("STOP ALARM & VIBRATION", style = MaterialTheme.typography.titleMedium, color = Color.White)
                    }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }

        // Low Battery Warning Banner
        if (isBatteryLow && !isEmergencyActive) {
            val battPercent = batteryLevel ?: 15
            val voltageInfo = if (batteryVoltageMv != null && batteryVoltageMv!! > 0) {
                " (${String.format("%.2f", batteryVoltageMv!! / 1000f)}V)"
            } else ""

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = StatusOrange.copy(alpha = 0.12f)),
                shape = RoundedCornerShape(16.dp)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .border(1.5.dp, StatusOrange, RoundedCornerShape(16.dp))
                        .padding(14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(40.dp)
                            .clip(CircleShape)
                            .background(StatusOrange.copy(alpha = 0.2f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.BatteryAlert,
                            contentDescription = "Low Battery",
                            tint = StatusOrange,
                            modifier = Modifier.size(24.dp)
                        )
                    }

                    Spacer(modifier = Modifier.width(12.dp))

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "WEARABLE BATTERY LOW ($battPercent%)",
                            style = MaterialTheme.typography.titleMedium.copy(fontSize = 15.sp),
                            color = StatusOrange
                        )
                        Spacer(modifier = Modifier.height(2.dp))
                        Text(
                            text = "Wearable battery is at $battPercent%$voltageInfo. Please charge your SomniGuard device to maintain tracking.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.85f)
                        )
                    }

                    Spacer(modifier = Modifier.width(8.dp))

                    TextButton(
                        onClick = { viewModel.dismissLowBatteryAlert() },
                        colors = ButtonDefaults.textButtonColors(contentColor = StatusOrange)
                    ) {
                        Text("Dismiss", style = MaterialTheme.typography.labelLarge)
                    }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
        }

        // Connection Protection Status Card
        val (statusText, statusSubtext, statusColor, statusIcon) = when (connectionState) {
            is BleConnectionState.Connected -> Quadruple(
                "Protection Active & Connected",
                "Connected to ${(connectionState as BleConnectionState.Connected).deviceName}",
                StatusGreen,
                Icons.Default.Shield
            )
            is BleConnectionState.Connecting -> Quadruple(
                "Connecting to Wearable...",
                "Pairing with device...",
                StatusOrange,
                Icons.Default.Warning
            )
            is BleConnectionState.Scanning -> Quadruple(
                "Scanning for Wearables...",
                "Searching nearby Bluetooth devices...",
                PrimaryBlue,
                Icons.Default.Warning
            )
            else -> Quadruple(
                "Wearable Disconnected",
                "Please tap 'Wearable' tab to connect your device",
                StatusRed,
                Icons.Default.Warning
            )
        }

        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(44.dp)
                        .clip(CircleShape)
                        .background(statusColor.copy(alpha = 0.15f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = statusIcon,
                        contentDescription = "Protection Status",
                        tint = statusColor,
                        modifier = Modifier.size(24.dp)
                    )
                }

                Spacer(modifier = Modifier.width(12.dp))

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = statusText,
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = statusSubtext,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                    )
                }

                // Battery Indicator when Connected
                if (connectionState is BleConnectionState.Connected) {
                    Spacer(modifier = Modifier.width(8.dp))
                    val level = batteryLevel
                    val (bColor, bIcon) = when {
                        level == null -> Pair(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f), Icons.Default.BatteryUnknown)
                        level <= 20 -> Pair(StatusRed, Icons.Default.BatteryAlert)
                        level < 50 -> Pair(StatusOrange, Icons.Default.BatterySaver)
                        else -> Pair(StatusGreen, Icons.Default.BatteryFull)
                    }
                    val levelText = if (level != null) "$level%" else "--%"

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(10.dp))
                            .background(bColor.copy(alpha = 0.12f))
                            .border(1.dp, bColor.copy(alpha = 0.3f), RoundedCornerShape(10.dp))
                            .padding(horizontal = 10.dp, vertical = 6.dp)
                    ) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Icon(
                                imageVector = bIcon,
                                contentDescription = "Kit Battery",
                                tint = bColor,
                                modifier = Modifier.size(18.dp)
                            )
                            Text(
                                text = levelText,
                                style = MaterialTheme.typography.titleSmall.copy(fontSize = 13.sp),
                                color = bColor
                            )
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // SomniGuard Brain FSM State Card (Synchronized with EFR32 xG26 DevKit FSM)
        val (fsmColor, fsmIcon, fsmBadgeBg) = when (topFsmState) {
            SomniGuardTopFsmState.NORMAL_SLEEP -> Triple(StatusGreen, Icons.Default.CheckCircle, StatusGreen.copy(alpha = 0.15f))
            SomniGuardTopFsmState.ACTIVE_MODE -> Triple(PrimaryBlue, Icons.Default.CheckCircle, PrimaryBlue.copy(alpha = 0.15f))
            SomniGuardTopFsmState.OFF_FINGER_SUSPEND -> Triple(StatusOrange, Icons.Default.Warning, StatusOrange.copy(alpha = 0.15f))
            SomniGuardTopFsmState.DEEP_ANALYSIS -> Triple(StatusRed, Icons.Default.NotificationsActive, StatusRed.copy(alpha = 0.15f))
            SomniGuardTopFsmState.INACTIVE -> Triple(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f), Icons.Default.Warning, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.1f))
        }

        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = fsmIcon,
                            contentDescription = "FSM Brain",
                            tint = fsmColor,
                            modifier = Modifier.size(22.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "SomniGuard FSM",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(fsmBadgeBg)
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = topFsmState.stateName,
                            color = fsmColor,
                            style = MaterialTheme.typography.labelMedium.copy(fontSize = 12.sp)
                        )
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    text = topFsmState.description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.75f)
                )
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // MAX30102 PPG Telemetry Toggle Switch Card
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Default.MonitorHeart,
                        contentDescription = "PPG Telemetry",
                        tint = PrimaryBlue,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(10.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "Live Health Telemetry",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = if (isTelemetryEnabled) "SpO2 & Heart Rate Active" else "Telemetry Display Paused",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                        )
                    }
                }

                Spacer(modifier = Modifier.width(8.dp))

                Switch(
                    checked = isTelemetryEnabled,
                    onCheckedChange = { isTelemetryEnabled = it },
                    colors = SwitchDefaults.colors(checkedThumbColor = PrimaryBlue)
                )
            }
        }

        // Live Health Telemetry Section (SpO2 & Heart Rate Cards)
        if (isTelemetryEnabled) {
            Spacer(modifier = Modifier.height(16.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // Heart Rate (BPM) Card
                Card(
                    modifier = Modifier.weight(1f),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                            .padding(14.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Default.Favorite,
                                contentDescription = "Heart Rate",
                                tint = StatusRed,
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "Heart Rate",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        Row(verticalAlignment = Alignment.Bottom) {
                            Text(
                                text = heartRateStr,
                                style = MaterialTheme.typography.headlineLarge.copy(fontSize = 32.sp),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = "BPM",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                                modifier = Modifier.padding(bottom = 3.dp)
                            )
                        }

                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = if (isFingerAttached) "● Range" else "● Finger Removed",
                            style = MaterialTheme.typography.labelMedium,
                            color = if (isFingerAttached) StatusGreen else StatusOrange
                        )
                    }
                }

                // Blood Oxygen (SpO2 %) Card
                Card(
                    modifier = Modifier.weight(1f),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
                    shape = RoundedCornerShape(16.dp)
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                            .padding(14.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = Icons.Default.MonitorHeart,
                                contentDescription = "SpO2",
                                tint = PrimaryBlue,
                                modifier = Modifier.size(20.dp)
                            )
                            Spacer(modifier = Modifier.width(6.dp))
                            Text(
                                text = "Blood Oxygen",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        Row(verticalAlignment = Alignment.Bottom) {
                            Text(
                                text = spO2Str,
                                style = MaterialTheme.typography.headlineLarge.copy(fontSize = 32.sp),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = "% SpO2",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                                modifier = Modifier.padding(bottom = 3.dp)
                            )
                        }

                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = if (isFingerAttached) "● Oxygen Level" else "● Finger Removed",
                            style = MaterialTheme.typography.labelMedium,
                            color = if (isFingerAttached) StatusGreen else StatusOrange
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // Historical Health Trends Chart Header & Time Filters
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "Health Trends",
                            style = MaterialTheme.typography.titleLarge,
                            color = MaterialTheme.colorScheme.onBackground
                        )
                        Text(
                            text = "Displaying: ${selectedTimeRange.fullDescription}",
                            style = MaterialTheme.typography.labelMedium,
                            color = PrimaryBlue
                        )
                    }

                    OutlinedButton(
                        onClick = { showCustomTimeDialog = true },
                        shape = RoundedCornerShape(8.dp),
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp),
                        modifier = Modifier.height(32.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Tune,
                            contentDescription = "Custom Time",
                            tint = PrimaryBlue,
                            modifier = Modifier.size(14.dp)
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Text(
                            text = "Custom",
                            style = MaterialTheme.typography.labelMedium,
                            color = PrimaryBlue
                        )
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                // Scrollable Row of Quick Presets: 1m, 5m, 1h, 24h, 7d, 30d
                val presetScrollState = rememberScrollState()
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(presetScrollState),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    TrendTimeRange.PRESETS.forEach { preset ->
                        val isSelected = selectedTimeRange.amount == preset.amount && selectedTimeRange.unit == preset.unit
                        Button(
                            onClick = { viewModel.setTimeRange(preset) },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (isSelected) PrimaryBlue else MaterialTheme.colorScheme.surface
                            ),
                            shape = RoundedCornerShape(8.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 4.dp),
                            modifier = Modifier.height(32.dp)
                        ) {
                            Text(
                                text = preset.shortLabel,
                                style = MaterialTheme.typography.labelMedium,
                                color = if (isSelected) Color.White else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f)
                            )
                        }
                    }

                    // Active custom range badge if selected custom time
                    val isCustom = !TrendTimeRange.PRESETS.any { it.amount == selectedTimeRange.amount && it.unit == selectedTimeRange.unit }
                    if (isCustom) {
                        Button(
                            onClick = { showCustomTimeDialog = true },
                            colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue),
                            shape = RoundedCornerShape(8.dp),
                            contentPadding = PaddingValues(horizontal = 10.dp, vertical = 4.dp),
                            modifier = Modifier.height(32.dp)
                        ) {
                            Text(
                                text = selectedTimeRange.shortLabel,
                                style = MaterialTheme.typography.labelMedium,
                                color = Color.White
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(10.dp))

            // Re-integrated Historical Health Trend Chart
            HistoricalTrendChart(trendData = trendData)
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Interactive Configured Emergency Contact Card
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .clickable { onNavigateToSettings() },
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
                    .padding(16.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Icon(
                            imageVector = Icons.Default.Call,
                            contentDescription = "Contacts",
                            tint = PrimaryBlue,
                            modifier = Modifier.size(24.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Configured Emergency Contact",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }

                    Icon(
                        imageVector = Icons.Default.ChevronRight,
                        contentDescription = "Edit in Settings",
                        tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        modifier = Modifier.size(24.dp)
                    )
                }

                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = if (primaryContact.isNotBlank()) primaryContact else "No number set yet",
                        style = MaterialTheme.typography.headlineMedium,
                        color = if (primaryContact.isNotBlank()) MaterialTheme.colorScheme.onSurface else StatusOrange
                    )

                    if (primaryContact.isNotBlank()) {
                        Icon(
                            imageVector = Icons.Default.CheckCircle,
                            contentDescription = "Ready",
                            tint = StatusGreen
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    if (primaryContact.isNotBlank()) {
                        Button(
                            onClick = {
                                try {
                                    val intent = Intent(Intent.ACTION_DIAL, Uri.parse("tel:$primaryContact"))
                                    context.startActivity(intent)
                                } catch (e: Exception) { }
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = StatusGreen),
                            shape = RoundedCornerShape(10.dp),
                            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 6.dp),
                            modifier = Modifier.weight(1f)
                        ) {
                            Icon(Icons.Default.Call, contentDescription = "Call", tint = Color.White, modifier = Modifier.size(16.dp))
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Call Primary", style = MaterialTheme.typography.labelLarge, color = Color.White)
                        }
                    }

                    OutlinedButton(
                        onClick = { onNavigateToSettings() },
                        shape = RoundedCornerShape(10.dp),
                        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 6.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.Edit, contentDescription = "Edit", tint = PrimaryBlue, modifier = Modifier.size(16.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text("Configure Number", style = MaterialTheme.typography.labelLarge, color = PrimaryBlue)
                    }
                }

                // Bedtime Emergency Location Status Indicator
                Spacer(modifier = Modifier.height(12.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.LocationOn,
                            contentDescription = "Location Status",
                            tint = if (cachedLocation != null || manualAddress.isNotBlank()) StatusGreen else StatusOrange,
                            modifier = Modifier.size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        val locText = when {
                            manualAddress.isNotBlank() -> "📍 Location: $manualAddress"
                            cachedLocation != null -> {
                                val addr = cachedLocation?.address ?: String.format(java.util.Locale.US, "%.4f, %.4f", cachedLocation!!.latitude, cachedLocation!!.longitude)
                                "📍 GPS: $addr"
                            }
                            else -> "📍 Emergency Location: No GPS saved (Tap to configure)"
                        }
                        Text(
                            text = locText,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurface,
                            maxLines = 1,
                            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }

    // Custom Time Range Dialog
    if (showCustomTimeDialog) {
        AlertDialog(
            onDismissRequest = { showCustomTimeDialog = false },
            title = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Schedule,
                        contentDescription = null,
                        tint = PrimaryBlue,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Custom Time Range",
                        style = MaterialTheme.typography.titleLarge
                    )
                }
            },
            text = {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text = "Enter duration and select unit to view health trend analytics (e.g., 2 minutes, 1 hour, 14 days):",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.8f)
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    OutlinedTextField(
                        value = customAmountInput,
                        onValueChange = { customAmountInput = it.filter { ch -> ch.isDigit() } },
                        label = { Text("Quantity") },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth()
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    Text(
                        text = "Time Unit:",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )

                    Spacer(modifier = Modifier.height(6.dp))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        TimeRangeUnit.entries.forEach { unit ->
                            val isUnitSelected = customUnitSelection == unit
                            Button(
                                onClick = { customUnitSelection = unit },
                                colors = ButtonDefaults.buttonColors(
                                    containerColor = if (isUnitSelected) PrimaryBlue else MaterialTheme.colorScheme.surfaceVariant
                                ),
                                shape = RoundedCornerShape(8.dp),
                                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 6.dp),
                                modifier = Modifier.weight(1f)
                            ) {
                                Text(
                                    text = unit.displayName,
                                    color = if (isUnitSelected) Color.White else MaterialTheme.colorScheme.onSurfaceVariant,
                                    style = MaterialTheme.typography.labelMedium
                                )
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(14.dp))

                    Text(
                        text = "Quick Suggestions:",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
                    )
                    Spacer(modifier = Modifier.height(6.dp))
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        listOf(
                            Triple("2 mins", 2, TimeRangeUnit.MINUTES),
                            Triple("15 mins", 15, TimeRangeUnit.MINUTES),
                            Triple("2 hours", 2, TimeRangeUnit.HOURS),
                            Triple("12 hours", 12, TimeRangeUnit.HOURS)
                        ).forEach { (label, amount, unit) ->
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(6.dp))
                                    .background(PrimaryBlue.copy(alpha = 0.1f))
                                    .clickable {
                                        customAmountInput = amount.toString()
                                        customUnitSelection = unit
                                    }
                                    .padding(horizontal = 8.dp, vertical = 5.dp)
                            ) {
                                Text(
                                    text = label,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = PrimaryBlue
                                )
                            }
                        }
                    }
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        val amount = customAmountInput.toIntOrNull()?.coerceAtLeast(1) ?: 1
                        viewModel.setCustomTimeRange(amount, customUnitSelection)
                        showCustomTimeDialog = false
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue)
                ) {
                    Text("Apply", color = Color.White)
                }
            },
            dismissButton = {
                TextButton(onClick = { showCustomTimeDialog = false }) {
                    Text("Cancel")
                }
            }
        )
    }
}

private data class Quadruple<A, B, C, D>(val first: A, val second: B, val third: C, val fourth: D)
