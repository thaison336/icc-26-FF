package com.example.blewearable.ble

import android.content.Context
import android.util.Log
import com.example.blewearable.data.StringeeConfigManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

class StringeeCalloutService(private val context: Context) {

    private val stringeeConfig = StringeeConfigManager(context)

    suspend fun makeEmergencyCallout(
        toPhoneNumber: String,
        customReason: String = "BLE Emergency Triggered"
    ): StringeeCallResult = withContext(Dispatchers.IO) {
        if (!stringeeConfig.isConfigured) {
            val err = "Stringee API not configured! Please enter Key SID, Key Secret, and From Number in App UI."
            Log.e(TAG, err)
            return@withContext StringeeCallResult.Error(err)
        }

        if (toPhoneNumber.isBlank()) {
            val err = "Primary Contact Phone Number is empty!"
            Log.e(TAG, err)
            return@withContext StringeeCallResult.Error(err)
        }

        try {
            val jwtToken = StringeeJwtHelper.generateJwtToken(
                stringeeConfig.keySid,
                stringeeConfig.keySecret
            )

            val formattedFrom = formatPhoneNumber(stringeeConfig.fromNumber)
            val formattedTo = formatPhoneNumber(toPhoneNumber)

            val url = URL("https://api.stringee.com/v1/call2/callout")
            val conn = url.openConnection() as HttpURLConnection
            conn.requestMethod = "POST"
            conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8")
            conn.setRequestProperty("X-STRINGEE-AUTH", jwtToken)
            conn.doOutput = true
            conn.doInput = true
            conn.connectTimeout = 10000
            conn.readTimeout = 10000

            // Build Stringee Callout Payload
            val requestBody = JSONObject().apply {
                // From object
                put("from", JSONObject().apply {
                    put("type", "external")
                    put("number", formattedFrom)
                    put("alias", formattedFrom)
                })

                // To array
                put("to", JSONArray().apply {
                    put(JSONObject().apply {
                        put("type", "external")
                        put("number", formattedTo)
                        put("alias", formattedTo)
                    })
                })

                // Actions array (Talk or Play)
                val actionsArray = JSONArray()
                if (stringeeConfig.voiceMode == StringeeConfigManager.MODE_AUDIO_URL) {
                    // Action 1: Play pre-recorded MP3 URL
                    actionsArray.put(JSONObject().apply {
                        put("action", "play")
                        put("fileName", stringeeConfig.audioUrl)
                    })
                } else {
                    // Action 2: Text-To-Speech AI Voice
                    val messageText = "${stringeeConfig.ttsText} [Reason: $customReason]"
                    actionsArray.put(JSONObject().apply {
                        put("action", "talk")
                        put("text", messageText)
                        put("voice", "northern") // Voice: northern / southern / female / male
                        put("speed", 0)
                    })
                }

                put("actions", actionsArray)
            }

            Log.d(TAG, "Dispatching Stringee Callout POST: ${requestBody.toString(2)}")

            OutputStreamWriter(conn.outputStream, "UTF-8").use { writer ->
                writer.write(requestBody.toString())
                writer.flush()
            }

            val responseCode = conn.responseCode
            val responseStream = if (responseCode in 200..299) conn.inputStream else conn.errorStream
            val responseText = responseStream.bufferedReader().use { it.readText() }

            Log.i(TAG, "Stringee Response (HTTP $responseCode): $responseText")

            if (responseCode in 200..299) {
                val jsonResp = JSONObject(responseText)
                val rCode = jsonResp.optInt("r", -1)
                val msg = jsonResp.optString("msg", "Unknown response")
                if (rCode == 0) {
                    val callId = jsonResp.optString("call_id", "N/A")
                    StringeeCallResult.Success(callId, "Stringee Auto Callout Dispatched to $formattedTo (Call ID: $callId)")
                } else {
                    val friendlyError = getFriendlyStringeeError(rCode, msg)
                    StringeeCallResult.Error(friendlyError)
                }
            } else {
                StringeeCallResult.Error("HTTP Error $responseCode: $responseText")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Exception making Stringee callout: ${e.message}", e)
            StringeeCallResult.Error("Failed to make callout: ${e.message}")
        }
    }
    private fun formatPhoneNumber(rawNumber: String): String {
        var cleaned = rawNumber.trim().replace(" ", "").replace("-", "")
        if (cleaned.startsWith("+")) {
            cleaned = cleaned.substring(1)
        }
        // Convert Vietnamese 09xxx / 03xxx / 07xxx / 08xxx to 84xxx format if needed
        if (cleaned.startsWith("0") && cleaned.length >= 10) {
            cleaned = "84" + cleaned.substring(1)
        }
        return cleaned
    }

    private fun getFriendlyStringeeError(rCode: Int, msg: String): String {
        return when (rCode) {
            1 -> "Stringee Error (r=1): Key SID or Key Secret is invalid/expired. Please re-check your Stringee API Key SID and Key Secret in settings."
            2 -> "Stringee Error (r=2): From/To Number invalid or From Number is not a registered Stringee Hotline. Make sure 'From Number' is assigned to your Stringee project dashboard."
            3 -> "Stringee Error (r=3): Account balance insufficient. Please top up your Stringee developer account."
            4 -> "Stringee Error (r=4): Access denied or Project inactive on Stringee Dashboard."
            else -> "Stringee API Error (r=$rCode): $msg"
        }
    }

    companion object {
        private const val TAG = "StringeeCalloutService"
    }
}

sealed class StringeeCallResult {
    data class Success(val callId: String, val message: String) : StringeeCallResult()
    data class Error(val errorReason: String) : StringeeCallResult()
}
