package com.example.blewearable.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.blewearable.data.BatchTrendSummary
import com.example.blewearable.ui.theme.AccentTeal
import com.example.blewearable.ui.theme.PrimaryBlue
import com.example.blewearable.ui.theme.SurfaceCard
import com.example.blewearable.ui.theme.SurfaceCardBorder
import com.example.blewearable.ui.theme.TextSecondary

@Composable
fun HistoricalTrendChart(
    trendData: List<BatchTrendSummary>,
    modifier: Modifier = Modifier
) {
    val textMeasurer = rememberTextMeasurer()

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(240.dp)
            .clip(RoundedCornerShape(16.dp))
            .background(SurfaceCard)
            .border(1.dp, SurfaceCardBorder, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        if (trendData.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(
                        text = "No Historical Data Saved Yet",
                        style = MaterialTheme.typography.bodyLarge,
                        color = TextSecondary
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Connect wearable or tap 'Generate Demo Data' below",
                        style = MaterialTheme.typography.labelMedium,
                        color = TextSecondary
                    )
                }
            }
        } else {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val width = size.width
                val height = size.height
                val paddingBottom = 40f
                val paddingTop = 20f
                val chartHeight = height - paddingBottom - paddingTop

                val maxValue = (trendData.maxOfOrNull { it.avgValue } ?: 100f).coerceAtLeast(10f) * 1.15f
                val barWidth = (width / trendData.size) * 0.5f
                val stepX = width / trendData.size

                // Draw background horizontal gridlines
                val gridCount = 4
                for (i in 0..gridCount) {
                    val y = paddingTop + (chartHeight / gridCount) * i
                    drawLine(
                        color = SurfaceCardBorder,
                        start = Offset(0f, y),
                        end = Offset(width, y),
                        strokeWidth = 1f
                    )
                }

                val linePath = Path()

                trendData.forEachIndexed { index, item ->
                    val xCenter = (index * stepX) + (stepX / 2f)
                    val barHeight = (item.avgValue / maxValue) * chartHeight
                    val yTop = height - paddingBottom - barHeight

                    // Draw Trend Bar Gradient
                    drawRoundRect(
                        brush = Brush.verticalGradient(
                            colors = listOf(AccentTeal, PrimaryBlue)
                        ),
                        topLeft = Offset(xCenter - (barWidth / 2f), yTop),
                        size = Size(barWidth, barHeight),
                        cornerRadius = CornerRadius(8f, 8f)
                    )

                    // Path for Trend Line
                    if (index == 0) {
                        linePath.moveTo(xCenter, yTop)
                    } else {
                        linePath.lineTo(xCenter, yTop)
                    }

                    // Draw X-axis Time Label
                    val labelResult = textMeasurer.measure(
                        text = item.timeLabel,
                        style = TextStyle(color = Color(0xFF94A3B8), fontSize = 10.sp)
                    )
                    drawText(
                        textLayoutResult = labelResult,
                        topLeft = Offset(xCenter - (labelResult.size.width / 2f), height - 28f)
                    )
                }

                // Draw connecting Trend Line overlay
                drawPath(
                    path = linePath,
                    color = Color.White.copy(alpha = 0.8f),
                    style = Stroke(width = 4f)
                )
            }
        }
    }
}
