package com.example.blewearable.ui.screens

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.CloudDone
import androidx.compose.material.icons.filled.OpenInNew
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

import androidx.core.content.ContextCompat
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
fun SettingsScreen(viewModel: MainViewModel) {
    val context = LocalContext.current
    val totalCount by viewModel.totalCount.collectAsState()

    var refreshTrigger by remember { mutableStateOf(0) }

    fun isPermissionGranted(permission: String): Boolean {
        return ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
    }

    val isBtEnabled = remember(refreshTrigger) { viewModel.bleManager.isBluetoothEnabled() }

    val permissionsList = remember(refreshTrigger) {
        val list = mutableListOf<Pair<String, Boolean>>()

        list.add("Bluetooth Hardware Power" to isBtEnabled)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            list.add("Bluetooth Scan Permission" to isPermissionGranted(Manifest.permission.BLUETOOTH_SCAN))
            list.add("Bluetooth Connect Permission" to isPermissionGranted(Manifest.permission.BLUETOOTH_CONNECT))
        } else {
            list.add("Location Access Permission" to isPermissionGranted(Manifest.permission.ACCESS_FINE_LOCATION))
        }

        list.add("Send Emergency SMS Permission" to isPermissionGranted(Manifest.permission.SEND_SMS))
        list.add("Place Auto Phone Calls Permission" to isPermissionGranted(Manifest.permission.CALL_PHONE))

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            list.add("Post Notifications Permission" to isPermissionGranted(Manifest.permission.POST_NOTIFICATIONS))
        }

        list
    }

    val allGranted = permissionsList.all { it.second }

    val requiredPermissionArray = remember {
        val list = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            list.add(Manifest.permission.BLUETOOTH_SCAN)
            list.add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            list.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        list.add(Manifest.permission.SEND_SMS)
        list.add(Manifest.permission.CALL_PHONE)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            list.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        list.toTypedArray()
    }

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) {
        refreshTrigger++
    }

    val enableBtLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        refreshTrigger++
    }

    fun openSystemAppSettings(context: Context) {
        val intent = Intent(
            Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
            Uri.fromParts("package", context.packageName, null)
        ).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK
        }
        context.startActivity(intent)
    }

    val scrollState = rememberScrollState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(DarkBackground)
            .verticalScroll(scrollState)
            .padding(16.dp)
    ) {
        Text(
            text = "App & Safety Settings",
            style = MaterialTheme.typography.headlineMedium
        )
        Spacer(modifier = Modifier.height(16.dp))

        // Permission & Hardware Status Card
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
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Default.Security,
                            contentDescription = "Security",
                            tint = PrimaryBlue
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "Permission & Bluetooth Status",
                            style = MaterialTheme.typography.titleMedium,
                            color = TextPrimary
                        )
                    }

                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(if (allGranted) StatusGreen.copy(alpha = 0.2f) else StatusOrange.copy(alpha = 0.2f))
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    ) {
                        Text(
                            text = if (allGranted) "All Ready" else "Action Needed",
                            color = if (allGranted) StatusGreen else StatusOrange,
                            style = MaterialTheme.typography.labelMedium
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))
                Text(
                    text = "If Bluetooth is OFF or permissions are missing, use the action buttons below to turn ON Bluetooth or grant permissions.",
                    style = MaterialTheme.typography.bodySmall,
                    color = TextSecondary
                )
                Spacer(modifier = Modifier.height(16.dp))

                permissionsList.forEach { (name, granted) ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = name,
                            style = MaterialTheme.typography.bodyMedium,
                            color = TextPrimary
                        )

                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                imageVector = if (granted) Icons.Default.CheckCircle else Icons.Default.Warning,
                                contentDescription = if (granted) "Granted/ON" else "Denied/OFF",
                                tint = if (granted) StatusGreen else StatusRed
                            )
                            Spacer(modifier = Modifier.width(4.dp))
                            Text(
                                text = if (granted) "ON / Granted" else "OFF / Denied",
                                style = MaterialTheme.typography.labelSmall,
                                color = if (granted) StatusGreen else StatusRed
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                if (!isBtEnabled) {
                    Button(
                        onClick = { enableBtLauncher.launch(viewModel.bleManager.getEnableBluetoothIntent()) },
                        colors = ButtonDefaults.buttonColors(containerColor = StatusOrange),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("Turn ON Bluetooth (System Prompt)")
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Button(
                        onClick = { launcher.launch(requiredPermissionArray) },
                        colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.Refresh, contentDescription = "Request")
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Re-Request")
                    }

                    OutlinedButton(
                        onClick = { openSystemAppSettings(context) },
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Default.OpenInNew, contentDescription = "Settings", tint = PrimaryBlue)
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("Open Settings", color = PrimaryBlue)
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // Database & Storage Card
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
                        imageVector = Icons.Default.Storage,
                        contentDescription = "Database",
                        tint = StatusGreen
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        text = "Data Storage & Architecture",
                        style = MaterialTheme.typography.titleMedium,
                        color = TextPrimary
                    )
                }
                Spacer(modifier = Modifier.height(12.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(text = "Database Type:", style = MaterialTheme.typography.bodyMedium, color = TextSecondary)
                    Text(text = "Android Room (SQLite)", style = MaterialTheme.typography.bodyMedium, color = TextPrimary)
                }

                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(text = "Storage Location:", style = MaterialTheme.typography.bodyMedium, color = TextSecondary)
                    Text(text = "Internal Phone Storage", style = MaterialTheme.typography.bodyMedium, color = StatusGreen)
                }

                Spacer(modifier = Modifier.height(8.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(text = "Stored Records:", style = MaterialTheme.typography.bodyMedium, color = TextSecondary)
                    Text(text = "$totalCount entries", style = MaterialTheme.typography.bodyMedium, color = TextPrimary)
                }

                Spacer(modifier = Modifier.height(12.dp))

                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(10.dp))
                        .background(PrimaryBlue.copy(alpha = 0.15f))
                        .padding(12.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.CloudDone, contentDescription = "Cloud Info", tint = PrimaryBlue)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = "100% Offline Local-First Storage. Your data stays private on this device.",
                            style = MaterialTheme.typography.bodySmall,
                            color = TextPrimary
                        )
                    }
                }
            }
        }
    }
}
