package com.example.blewearable.data

import android.content.Context
import android.content.SharedPreferences

class EmergencyContactManager(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    var primaryContact: String
        get() = prefs.getString(KEY_PRIMARY_CONTACT, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_PRIMARY_CONTACT, value.trim()).apply()
        }

    var secondaryContact: String
        get() = prefs.getString(KEY_SECONDARY_CONTACT, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_SECONDARY_CONTACT, value.trim()).apply()
        }

    var customSosMessage: String
        get() = prefs.getString(
            KEY_SOS_MESSAGE,
            DEFAULT_SOS_MESSAGE
        ) ?: DEFAULT_SOS_MESSAGE
        set(value) {
            prefs.edit().putString(KEY_SOS_MESSAGE, value.trim()).apply()
        }

    // Manual user-entered current address (e.g., Room 302, Sunrise Apt, District 7, HCMC)
    var manualAddress: String
        get() = prefs.getString(KEY_MANUAL_ADDRESS, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_MANUAL_ADDRESS, value.trim()).apply()
        }

    // Location Dispatch Mode: "BOTH" (default), "AUTO_GPS", "MANUAL"
    var locationPreferenceMode: String
        get() = prefs.getString(KEY_LOCATION_MODE, MODE_BOTH) ?: MODE_BOTH
        set(value) {
            prefs.edit().putString(KEY_LOCATION_MODE, value).apply()
        }

    // Cached GPS coordinates & geocoded address
    var cachedLatitude: Double
        get() = java.lang.Double.longBitsToDouble(prefs.getLong(KEY_CACHED_LAT, java.lang.Double.doubleToLongBits(0.0)))
        set(value) {
            prefs.edit().putLong(KEY_CACHED_LAT, java.lang.Double.doubleToLongBits(value)).apply()
        }

    var cachedLongitude: Double
        get() = java.lang.Double.longBitsToDouble(prefs.getLong(KEY_CACHED_LNG, java.lang.Double.doubleToLongBits(0.0)))
        set(value) {
            prefs.edit().putLong(KEY_CACHED_LNG, java.lang.Double.doubleToLongBits(value)).apply()
        }

    var cachedAddressText: String
        get() = prefs.getString(KEY_CACHED_ADDRESS, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_CACHED_ADDRESS, value.trim()).apply()
        }

    var cachedLocationTimestamp: Long
        get() = prefs.getLong(KEY_CACHED_TIME, 0L)
        set(value) {
            prefs.edit().putLong(KEY_CACHED_TIME, value).apply()
        }

    fun saveCachedLocation(info: UserLocationInfo) {
        cachedLatitude = info.latitude
        cachedLongitude = info.longitude
        cachedAddressText = info.address ?: ""
        cachedLocationTimestamp = info.timestamp
    }

    fun getCachedLocationInfo(): UserLocationInfo? {
        if (cachedLatitude == 0.0 && cachedLongitude == 0.0) return null
        return UserLocationInfo(
            latitude = cachedLatitude,
            longitude = cachedLongitude,
            address = cachedAddressText.ifBlank { null },
            mapsUrl = "https://maps.google.com/?q=$cachedLatitude,$cachedLongitude",
            timestamp = cachedLocationTimestamp
        )
    }

    /**
     * Formats location information into a clear, structured snippet for inclusion in emergency SMS.
     */
    fun formatLocationForSms(): String {
        val hasManual = manualAddress.isNotBlank()
        val hasGps = cachedLatitude != 0.0 && cachedLongitude != 0.0
        val mapsUrl = if (hasGps) "https://maps.google.com/?q=$cachedLatitude,$cachedLongitude" else ""

        val timeStr = if (cachedLocationTimestamp > 0L) {
            try {
                val sdf = java.text.SimpleDateFormat("HH:mm dd/MM", java.util.Locale.getDefault())
                sdf.format(java.util.Date(cachedLocationTimestamp))
            } catch (e: Exception) { "" }
        } else ""

        val sb = StringBuilder()

        when (locationPreferenceMode) {
            MODE_MANUAL -> {
                if (hasManual) {
                    sb.append("📍 Location: $manualAddress")
                } else if (hasGps) {
                    // Fallback to GPS if manual is blank
                    val addr = if (cachedAddressText.isNotBlank()) cachedAddressText else "$cachedLatitude, $cachedLongitude"
                    sb.append("📍 Location (GPS): $addr\n🗺️ Maps: $mapsUrl")
                    if (timeStr.isNotBlank()) sb.append(" (at $timeStr)")
                }
            }
            MODE_AUTO_GPS -> {
                if (hasGps) {
                    val addr = if (cachedAddressText.isNotBlank()) cachedAddressText else "$cachedLatitude, $cachedLongitude"
                    sb.append("📍 Location (GPS): $addr\n🗺️ Maps: $mapsUrl")
                    if (timeStr.isNotBlank()) sb.append(" (at $timeStr)")
                } else if (hasManual) {
                    // Fallback to manual if GPS is missing
                    sb.append("📍 Location: $manualAddress")
                }
            }
            else -> { // MODE_BOTH
                if (hasManual && hasGps) {
                    sb.append("📍 Location: $manualAddress")
                    if (cachedAddressText.isNotBlank() && !cachedAddressText.equals(manualAddress, ignoreCase = true)) {
                        sb.append(" (~$cachedAddressText)")
                    }
                    sb.append("\n🗺️ Maps: $mapsUrl")
                    if (timeStr.isNotBlank()) sb.append(" (at $timeStr)")
                } else if (hasManual) {
                    sb.append("📍 Location: $manualAddress")
                } else if (hasGps) {
                    val addr = if (cachedAddressText.isNotBlank()) cachedAddressText else "$cachedLatitude, $cachedLongitude"
                    sb.append("📍 Location (GPS): $addr\n🗺️ Maps: $mapsUrl")
                    if (timeStr.isNotBlank()) sb.append(" (at $timeStr)")
                }
            }
        }

        return sb.toString().trim()
    }

    fun getAllContacts(): List<String> {
        val list = mutableListOf<String>()
        if (primaryContact.isNotBlank()) list.add(primaryContact)
        if (secondaryContact.isNotBlank()) list.add(secondaryContact)
        return list.distinct()
    }

    companion object {
        private const val PREF_NAME = "emergency_prefs"
        private const val KEY_PRIMARY_CONTACT = "primary_contact"
        private const val KEY_SECONDARY_CONTACT = "secondary_contact"
        private const val KEY_SOS_MESSAGE = "sos_message"
        private const val KEY_MANUAL_ADDRESS = "manual_address"
        private const val KEY_LOCATION_MODE = "location_mode"
        private const val KEY_CACHED_LAT = "cached_lat"
        private const val KEY_CACHED_LNG = "cached_lng"
        private const val KEY_CACHED_ADDRESS = "cached_address"
        private const val KEY_CACHED_TIME = "cached_time"

        const val MODE_BOTH = "BOTH"
        const val MODE_AUTO_GPS = "AUTO_GPS"
        const val MODE_MANUAL = "MANUAL"

        const val DEFAULT_SOS_MESSAGE =
            "EMERGENCY SOS ALERT! The wearable watch triggered an emergency alert. Please check on the user immediately or call emergency services!"
    }
}
