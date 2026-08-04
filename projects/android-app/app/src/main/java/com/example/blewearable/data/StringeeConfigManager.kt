package com.example.blewearable.data

import android.content.Context
import android.content.SharedPreferences

class StringeeConfigManager(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    var keySid: String
        get() = prefs.getString(KEY_SID, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_SID, value.trim()).apply()
        }

    var keySecret: String
        get() = prefs.getString(KEY_SECRET, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_SECRET, value.trim()).apply()
        }

    var fromNumber: String
        get() = prefs.getString(KEY_FROM_NUMBER, "") ?: ""
        set(value) {
            prefs.edit().putString(KEY_FROM_NUMBER, value.trim()).apply()
        }

    // Voice mode: "TTS" (Text-to-Speech) or "AUDIO_URL" (MP3 Link)
    var voiceMode: String
        get() = prefs.getString(KEY_VOICE_MODE, MODE_TTS) ?: MODE_TTS
        set(value) {
            prefs.edit().putString(KEY_VOICE_MODE, value.trim()).apply()
        }

    var audioUrl: String
        get() = prefs.getString(KEY_AUDIO_URL, DEFAULT_AUDIO_URL) ?: DEFAULT_AUDIO_URL
        set(value) {
            prefs.edit().putString(KEY_AUDIO_URL, value.trim()).apply()
        }

    var ttsText: String
        get() = prefs.getString(KEY_TTS_TEXT, DEFAULT_TTS_TEXT) ?: DEFAULT_TTS_TEXT
        set(value) {
            prefs.edit().putString(KEY_TTS_TEXT, value.trim()).apply()
        }

    val isConfigured: Boolean
        get() = keySid.isNotBlank() && keySecret.isNotBlank() && fromNumber.isNotBlank()

    companion object {
        private const val PREF_NAME = "stringee_config_prefs"
        private const val KEY_SID = "key_sid"
        private const val KEY_SECRET = "key_secret"
        private const val KEY_FROM_NUMBER = "from_number"
        private const val KEY_VOICE_MODE = "voice_mode"
        private const val KEY_AUDIO_URL = "audio_url"
        private const val KEY_TTS_TEXT = "tts_text"

        const val MODE_TTS = "TTS"
        const val MODE_AUDIO_URL = "AUDIO_URL"

        const val DEFAULT_TTS_TEXT =
            "Cảnh báo khẩn cấp! Nạn nhân đeo thiết bị vừa phát tín hiệu SOS nguy hiểm. Vui lòng kiểm tra và hỗ trợ ngay lập tức!"
        const val DEFAULT_AUDIO_URL =
            "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3"
    }
}
