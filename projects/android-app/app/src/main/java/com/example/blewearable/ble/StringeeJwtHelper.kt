package com.example.blewearable.ble

import android.util.Base64
import org.json.JSONObject
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

object StringeeJwtHelper {

    fun generateJwtToken(keySid: String, keySecret: String): String {
        val nowSec = System.currentTimeMillis() / 1000
        val expSec = nowSec + 3600 // Valid for 1 hour

        // Header JSON
        val headerJson = JSONObject().apply {
            put("typ", "JWT")
            put("alg", "HS256")
            put("cty", "stringee-api;v=1")
        }

        // Payload JSON
        val payloadJson = JSONObject().apply {
            put("jti", "$keySid-$nowSec")
            put("iss", keySid)
            put("iat", nowSec)
            put("exp", expSec)
            put("rest_api", true)
        }

        val encodedHeader = base64UrlEncode(headerJson.toString().toByteArray(Charsets.UTF_8))
        val encodedPayload = base64UrlEncode(payloadJson.toString().toByteArray(Charsets.UTF_8))

        val contentToSign = "$encodedHeader.$encodedPayload"
        val signature = hmacSha256(contentToSign, keySecret)

        return "$contentToSign.$signature"
    }

    private fun base64UrlEncode(input: ByteArray): String {
        return Base64.encodeToString(input, Base64.NO_WRAP or Base64.NO_PADDING or Base64.URL_SAFE)
    }

    private fun hmacSha256(data: String, secret: String): String {
        val sha256HMAC = Mac.getInstance("HmacSHA256")
        val secretKey = SecretKeySpec(secret.toByteArray(Charsets.UTF_8), "HmacSHA256")
        sha256HMAC.init(secretKey)
        val hash = sha256HMAC.doFinal(data.toByteArray(Charsets.UTF_8))
        return base64UrlEncode(hash)
    }
}
