package com.example.blewearable.data

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Address
import android.location.Geocoder
import android.location.Location
import android.location.LocationManager
import android.os.Build
import android.os.CancellationSignal
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.util.Locale
import kotlin.coroutines.resume

data class UserLocationInfo(
    val latitude: Double,
    val longitude: Double,
    val address: String?,
    val mapsUrl: String,
    val timestamp: Long
)

class LocationHelper(private val context: Context) {

    private val locationManager: LocationManager? =
        context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager

    fun hasLocationPermission(): Boolean {
        val fineLocation = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        val coarseLocation = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        return fineLocation || coarseLocation
    }

    fun isLocationServiceEnabled(): Boolean {
        if (locationManager == null) return false
        val gpsEnabled = try {
            locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
        } catch (e: Exception) {
            false
        }
        val networkEnabled = try {
            locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
        } catch (e: Exception) {
            false
        }
        return gpsEnabled || networkEnabled
    }

    @SuppressLint("MissingPermission")
    suspend fun fetchCurrentOrLastLocation(): UserLocationInfo? {
        if (!hasLocationPermission()) {
            Log.w("LocationHelper", "Location permissions are not granted.")
            return null
        }
        if (locationManager == null) {
            Log.w("LocationHelper", "LocationManager service unavailable.")
            return null
        }

        // 1. Get best last-known location across available providers
        var bestLastLocation: Location? = null
        try {
            val providers = locationManager.getProviders(true)
            for (provider in providers) {
                val loc = locationManager.getLastKnownLocation(provider) ?: continue
                if (bestLastLocation == null || loc.accuracy < bestLastLocation.accuracy) {
                    bestLastLocation = loc
                }
            }
        } catch (e: Exception) {
            Log.e("LocationHelper", "Error querying last known location: ${e.message}", e)
        }

        // 2. If modern Android 30+ (API 30+), attempt a quick current fix with timeout
        var freshLocation: Location? = null
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && isLocationServiceEnabled()) {
            freshLocation = withTimeoutOrNull(4000L) {
                suspendCancellableCoroutine { continuation ->
                    val signal = CancellationSignal()
                    continuation.invokeOnCancellation { signal.cancel() }
                    try {
                        val provider = when {
                            locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER) -> LocationManager.GPS_PROVIDER
                            locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER) -> LocationManager.NETWORK_PROVIDER
                            else -> LocationManager.PASSIVE_PROVIDER
                        }
                        locationManager.getCurrentLocation(
                            provider,
                            signal,
                            context.mainExecutor
                        ) { loc ->
                            if (continuation.isActive) {
                                continuation.resume(loc)
                            }
                        }
                    } catch (e: Exception) {
                        Log.w("LocationHelper", "getCurrentLocation failed: ${e.message}")
                        if (continuation.isActive) continuation.resume(null)
                    }
                }
            }
        }

        val targetLocation = freshLocation ?: bestLastLocation ?: return null
        val lat = targetLocation.latitude
        val lng = targetLocation.longitude
        val mapsUrl = "https://maps.google.com/?q=$lat,$lng"

        // 3. Reverse geocode asynchronously
        val addressText = reverseGeocode(lat, lng)

        return UserLocationInfo(
            latitude = lat,
            longitude = lng,
            address = addressText,
            mapsUrl = mapsUrl,
            timestamp = System.currentTimeMillis()
        )
    }

    private suspend fun reverseGeocode(latitude: Double, longitude: Double): String? =
        withContext(Dispatchers.IO) {
            if (!Geocoder.isPresent()) return@withContext null
            try {
                val geocoder = Geocoder(context, Locale.getDefault())
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    suspendCancellableCoroutine { cont ->
                        geocoder.getFromLocation(
                            latitude,
                            longitude,
                            1,
                            object : Geocoder.GeocodeListener {
                                override fun onGeocode(addresses: MutableList<Address>) {
                                    val first = addresses.firstOrNull()
                                    val text = first?.getAddressLine(0)
                                        ?: buildAddressString(first)
                                    if (cont.isActive) cont.resume(text)
                                }

                                override fun onError(errorMessage: String?) {
                                    Log.w("LocationHelper", "Geocoder error: $errorMessage")
                                    if (cont.isActive) cont.resume(null)
                                }
                            }
                        )
                    }
                } else {
                    @Suppress("DEPRECATION")
                    val list = geocoder.getFromLocation(latitude, longitude, 1)
                    val first = list?.firstOrNull()
                    first?.getAddressLine(0) ?: buildAddressString(first)
                }
            } catch (e: Exception) {
                Log.w("LocationHelper", "Reverse geocoding failed: ${e.message}")
                null
            }
        }

    private fun buildAddressString(address: Address?): String? {
        if (address == null) return null
        val parts = listOfNotNull(
            address.thoroughfare,
            address.subLocality,
            address.locality ?: address.subAdminArea,
            address.adminArea
        )
        return if (parts.isNotEmpty()) parts.joinToString(", ") else null
    }
}

