package com.example.blewearable.ble

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.Ringtone
import android.media.RingtoneManager
import android.net.Uri
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.telephony.SmsManager
import android.util.Log
import com.example.blewearable.data.EmergencyContactManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class EmergencyDispatcher(private val context: Context) {

    // Toggle for sound alert (Can be toggled in testing settings)
    var enableSoundAlert: Boolean = true

    val contactManager = EmergencyContactManager(context)
    private var ringtone: Ringtone? = null
    private var vibrator: Vibrator? = null

    private val _isEmergencyActive = MutableStateFlow(false)
    val isEmergencyActive: StateFlow<Boolean> = _isEmergencyActive.asStateFlow()

    private val _lastEmergencyLog = MutableStateFlow<String?>(null)
    val lastEmergencyLog: StateFlow<String?> = _lastEmergencyLog.asStateFlow()

    var isDismissedByUser: Boolean = false
        private set

    fun resetDismissedState() {
        if (isDismissedByUser) {
            Log.d("EmergencyDispatcher", "Resetting emergency dismissed flag as state returned to normal.")
            isDismissedByUser = false
        }
    }

    @SuppressLint("MissingPermission")
    fun triggerEmergency(triggerReason: String = "Wearable SOS Button Pressed", force: Boolean = false) {
        if (isDismissedByUser && !force) {
            Log.d("EmergencyDispatcher", "Emergency trigger skipped because user already stopped the alert for this session.")
            return
        }
        if (_isEmergencyActive.value && !force) {
            return
        }

        isDismissedByUser = false
        Log.w("EmergencyDispatcher", "🚨 EMERGENCY TRIGGERED: $triggerReason")
        _isEmergencyActive.value = true
        _lastEmergencyLog.value = "🚨 Emergency Alert Active: $triggerReason"

        // 1. Play loud phone alarm & trigger vibration
        startRingingAndVibration()

        // 2. Dispatch SMS to emergency contacts
        sendEmergencySms(triggerReason)

        // 3. Initiate native phone call to primary emergency contact
        initiateEmergencyCall()
    }

    private fun startRingingAndVibration() {
        try {
            if (enableSoundAlert) {
                val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
                audioManager?.let { am ->
                    // Ensure alarm volume is set high
                    val maxVol = am.getStreamMaxVolume(AudioManager.STREAM_ALARM)
                    am.setStreamVolume(AudioManager.STREAM_ALARM, maxVol, 0)
                }

                // Stop any previous instance before starting new
                ringtone?.stop()
                ringtone = null

                val alarmUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
                    ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)

                ringtone = RingtoneManager.getRingtone(context, alarmUri)?.apply {
                    audioAttributes = AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                    play()
                }
            } else {
                Log.d("EmergencyDispatcher", "Sound alert is disabled - running in silent vibration-only mode.")
            }

            // Stop any previous vibration before starting new
            vibrator?.cancel()
            vibrator = null

            // Vibration pattern: [delay, vibrate, sleep, vibrate, ...]
            val pattern = longArrayOf(0, 1000, 500, 1000, 500, 1000)
            vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vibratorManager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
                vibratorManager.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            }

            vibrator?.let { v ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createWaveform(pattern, 0))
                } else {
                    @Suppress("DEPRECATION")
                    v.vibrate(pattern, 0)
                }
            }
        } catch (e: Exception) {
            Log.e("EmergencyDispatcher", "Error starting sound/vibration: ${e.message}", e)
        }
    }

    fun stopEmergencyAlert() {
        try {
            isDismissedByUser = true
            _isEmergencyActive.value = false

            ringtone?.let {
                if (it.isPlaying) {
                    it.stop()
                }
            }
            ringtone = null

            vibrator?.cancel()
            vibrator = null

            Log.d("EmergencyDispatcher", "Emergency alert stopped by user (dismissed flag set to prevent BLE stream re-trigger).")
        } catch (e: Exception) {
            Log.e("EmergencyDispatcher", "Error stopping alert: ${e.message}", e)
        }
    }

    fun buildEmergencySmsMessage(reason: String): String {
        val baseMessage = "${contactManager.customSosMessage}\n[Reason: $reason]"
        val locationInfo = contactManager.formatLocationForSms()
        return if (locationInfo.isNotBlank()) {
            "$baseMessage\n$locationInfo"
        } else {
            baseMessage
        }
    }

    private fun sendEmergencySms(reason: String) {
        val contacts = contactManager.getAllContacts()
        if (contacts.isEmpty()) {
            Log.w("EmergencyDispatcher", "No emergency contacts configured for SMS!")
            return
        }

        val message = buildEmergencySmsMessage(reason)

        for (phoneNumber in contacts) {
            try {
                val smsManager: SmsManager = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    context.getSystemService(SmsManager::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    SmsManager.getDefault()
                }

                // If text message is long, split into parts
                val parts = smsManager.divideMessage(message)
                if (parts.size > 1) {
                    smsManager.sendMultipartTextMessage(phoneNumber, null, parts, null, null)
                } else {
                    smsManager.sendTextMessage(phoneNumber, null, message, null, null)
                }
                Log.i("EmergencyDispatcher", "Emergency SMS sent to $phoneNumber")
            } catch (e: Exception) {
                Log.e("EmergencyDispatcher", "Failed to send SMS to $phoneNumber: ${e.message}", e)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun initiateEmergencyCall() {
        val primaryContact = contactManager.primaryContact
        if (primaryContact.isBlank()) {
            Log.w("EmergencyDispatcher", "No primary emergency contact phone number configured for SOS call!")
            return
        }

        try {
            val callIntent = Intent(Intent.ACTION_CALL).apply {
                data = Uri.parse("tel:$primaryContact")
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            context.startActivity(callIntent)
            Log.i("EmergencyDispatcher", "Initiated Emergency Phone Call to $primaryContact")
        } catch (e: Exception) {
            Log.e("EmergencyDispatcher", "Failed to place call to $primaryContact: ${e.message}", e)
            // Fallback to dialer screen if direct ACTION_CALL is blocked by OS permissions
            try {
                val dialIntent = Intent(Intent.ACTION_DIAL).apply {
                    data = Uri.parse("tel:$primaryContact")
                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                }
                context.startActivity(dialIntent)
            } catch (fallbackError: Exception) {
                Log.e("EmergencyDispatcher", "Fallback dialer also failed", fallbackError)
            }
        }
    }
}
