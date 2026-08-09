package com.example.blewearable.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Watch
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.example.blewearable.ble.BleDeviceModel
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.StatusGreen
import com.example.blewearable.ui.theme.StatusOrange

@Composable
fun DeviceItem(
    device: BleDeviceModel,
    onConnect: (String) -> Unit
) {
    // Convert raw -dBm RSSI to human-friendly signal strength text & badge color
    val (signalLabel, signalColor) = when {
        device.rssi >= -60 -> "Strong Signal 📶" to StatusGreen
        device.rssi >= -75 -> "Good Signal 📶" to PrimaryBlue
        else -> "Weak Signal 📶" to StatusOrange
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surface)
            .border(1.5.dp, MaterialTheme.colorScheme.outlineVariant, RoundedCornerShape(16.dp))
            .clickable { onConnect(device.address) }
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Device Avatar Icon Container
        Box(
            modifier = Modifier
                .size(48.dp)
                .clip(CircleShape)
                .background(PrimaryBlue.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Default.Watch,
                contentDescription = "Wearable Device",
                tint = PrimaryBlue,
                modifier = Modifier.size(28.dp)
            )
        }

        Spacer(modifier = Modifier.width(16.dp))

        // Device Info
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onSurface
            )
            Spacer(modifier = Modifier.height(2.dp))
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(6.dp))
                    .background(signalColor.copy(alpha = 0.15f))
                    .padding(horizontal = 8.dp, vertical = 2.dp)
            ) {
                Text(
                    text = signalLabel,
                    style = MaterialTheme.typography.labelMedium,
                    color = signalColor
                )
            }
        }

        Spacer(modifier = Modifier.width(8.dp))

        // Large Connect Button for Elderly Users
        Button(
            onClick = { onConnect(device.address) },
            colors = ButtonDefaults.buttonColors(containerColor = PrimaryBlue),
            shape = RoundedCornerShape(12.dp)
        ) {
            Text("Connect", style = MaterialTheme.typography.labelLarge, color = Color.White)
        }
    }
}
