package com.example.blewearable

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import com.example.blewearable.ui.components.PermissionHandler
import com.example.blewearable.ui.screens.MainScreen
import com.example.blewearable.ui.theme.BleWearableTheme
import com.example.blewearable.viewmodel.MainViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            BleWearableTheme {
                PermissionHandler(
                    onPermissionsGranted = {
                        viewModel.startScan()
                    }
                ) {
                    MainScreen(viewModel = viewModel)
                }
            }
        }
    }
}
