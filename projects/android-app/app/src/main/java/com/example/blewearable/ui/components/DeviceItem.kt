package com.example.blewearable.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import com.example.blewearable.ble.BleDeviceModel
import com.example.blewearable.ui.theme.AccentTeal
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.SurfaceCard
import com.example.blewearable.ui.theme.SurfaceCardBorder
import com.example.blewearable.ui.theme.TextPrimary
import com.example.blewearable.ui.theme.TextSecondary

@Composable
fun DeviceItem(
    device: BleDeviceModel,
    onConnect: (String) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(SurfaceCard)
            .border(1.dp, SurfaceCardBorder, RoundedCornerShape(12.dp))
            .clickable { onConnect(device.address) }
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = Icons.Default.Bluetooth,
            contentDescription = "Bluetooth Device",
            tint = PrimaryBlue
        )
        Spacer(modifier = Modifier.width(16.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleLarge
            )
            Text(
                text = device.address,
                style = MaterialTheme.typography.bodyMedium
            )
        }
        Text(
            text = "${device.rssi} dBm",
            style = MaterialTheme.typography.labelMedium,
            color = AccentTeal
        )
    }
}
