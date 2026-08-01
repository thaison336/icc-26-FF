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
        const val DEFAULT_SOS_MESSAGE =
            "EMERGENCY SOS ALERT! The wearable watch triggered an emergency alert. Please check on the user immediately or call emergency services!"
    }
}
