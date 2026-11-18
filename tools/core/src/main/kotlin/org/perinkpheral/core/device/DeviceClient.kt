// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import java.awt.image.BufferedImage
import java.io.IOException
import org.perinkpheral.core.Json
import java.net.URI
import java.net.URLEncoder
import java.net.HttpURLConnection
import java.nio.charset.StandardCharsets
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CancellationException
import java.util.concurrent.ExecutionException
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException

/** The panel's frame as rows of pixels, true where there is ink; its number, and whether a
 * newer one is waiting for the panel. */
class Screen(val rows: List<BooleanArray>, val number: Int, val isPending: Boolean) {
    /** The frame as a picture in the panel's colours, a pixel a pixel. */
    fun image(): BufferedImage {
        val picture = BufferedImage(rows[0].size, rows.size, BufferedImage.TYPE_INT_RGB)
        rows.forEachIndexed { y, row -> row.forEachIndexed { x, isInk -> picture.setRGB(x, y, if (isInk) INK else PAPER) } }
        return picture
    }

    private companion object {
        const val INK = 0x141413
        const val PAPER = 0xE9E7E1
    }
}

/** A part of the device log. `end` is the position to pass to the next call. */
class DeviceLog(val text: String, val end: Long)

enum class DeviceButton(val number: Int) { UP(1), OK(2), DOWN(3), BACK(4) }

/**
 * HTTP client for the development API of a device (`/api/dev`, `/api/status`, `/api/log`).
 * The API is present in firmware built with APP_DEV_CONSOLE (the default).
 * All calls block; call them from a background thread.
 *
 * One connection per request (`Connection: close`): the device closes idle
 * connections, and a reused one fails with "header parser received no bytes".
 */
class DeviceClient(val host: String) {
    private val base = "http://" + host.trim().removePrefix("http://").trimEnd('/')

    @Volatile
    private var current: HttpURLConnection? = null

    @Volatile
    private var isAborted = false

    @Volatile
    private var firmwareRequest: CompletableFuture<HttpResponse<ByteArray>>? = null

    /** Cuts the request under way, an upload of minutes included, and refuses the ones after:
     * for whoever stops the work. */
    fun abort() {
        isAborted = true
        current?.disconnect()
        firmwareRequest?.cancel(true)
    }

    private class Response(val body: ByteArray, private val connection: HttpURLConnection) {
        fun header(name: String): String? = connection.getHeaderField(name)
    }

    private fun send(path: String, method: String = "GET", body: ByteArray? = null,
                     timeoutMs: Int = 10_000): Response {
        // 503: the host loop was busy, with the panel most likely: asked again a few times.
        for (attempt in 0 until BUSY_RETRIES) {
            try {
                return sendOnce(path, method, body, timeoutMs)
            } catch (busy: BusyException) {
                Thread.sleep(1_000)
            }
        }
        return sendOnce(path, method, body, timeoutMs)
    }

    private class BusyException : IOException("busy")

    private fun sendOnce(path: String, method: String, body: ByteArray?, timeoutMs: Int): Response {
        if (isAborted) throw InterruptedException("cancelled")
        val connection = URI.create(base + path).toURL().openConnection() as HttpURLConnection
        current = connection
        try {
            connection.requestMethod = method
            connection.connectTimeout = 3_000
            connection.readTimeout = timeoutMs
            connection.useCaches = false
            connection.setRequestProperty("Connection", "close")
            if (method != "GET") {
                val bytes = body ?: ByteArray(0)
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(bytes.size)
                connection.outputStream.use { it.write(bytes) }
            }
            val code = connection.responseCode
            if (code == 503) throw BusyException()
            if (code !in 200..299) {
                val said = connection.errorStream?.use { String(it.readBytes(), StandardCharsets.UTF_8) }.orEmpty()
                throw IOException("HTTP $code from $path: ${said.take(200)}")
            }
            return Response(connection.inputStream.use { it.readBytes() }, connection)
        } catch (error: IOException) {
            // A request cut by abort() is not a failure of the device.
            if (isAborted) throw InterruptedException("cancelled")
            throw error
        } finally {
            current = null
            connection.disconnect()
        }
    }

    /** An answer as it came: for whoever passes it on. */
    class Answer(val status: Int, val body: ByteArray, val headers: Map<String, String>)

    /** Asks once and gives back whatever the device answered, a refusal included. */
    fun raw(path: String, method: String = "GET"): Answer {
        val connection = URI.create(base + path).toURL().openConnection() as HttpURLConnection
        try {
            connection.requestMethod = method
            connection.connectTimeout = 3_000
            connection.readTimeout = 10_000
            connection.setRequestProperty("Connection", "close")
            if (method != "GET") {
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(0)
                connection.outputStream.close()
            }
            val code = connection.responseCode
            val stream = if (code in 200..299) connection.inputStream else connection.errorStream
            val body = stream?.use { it.readBytes() } ?: ByteArray(0)
            val headers = connection.headerFields.filterKeys { it != null }.mapValues { it.value.firstOrNull().orEmpty() }
            return Answer(code, body, headers)
        } finally {
            connection.disconnect()
        }
    }

    /** The JSON text of `/api/status`. */
    fun status(): String = String(send("/api/status").body, StandardCharsets.UTF_8)

    fun press(button: DeviceButton, isLong: Boolean) {
        val press = if (isLong) "long" else "short"
        send("/api/dev/button?button=${button.number}&press=$press", "POST")
    }

    fun open(application: String) {
        send("/api/dev/open?app=" + URLEncoder.encode(application, StandardCharsets.UTF_8), "POST")
    }

    /** Log text written after position `since`; the whole buffer when `since` is null. */
    fun log(since: Long?): DeviceLog {
        val response = send("/api/log" + (if (since != null) "?since=$since" else ""))
        return DeviceLog(String(response.body, StandardCharsets.UTF_8), response.header("X-Log-End")?.toLongOrNull() ?: 0L)
    }

    /** A request of the simulator's own (`/api/sim/...`): POST, the answer ignored. */
    fun simulator(path: String) {
        send("/api/sim/$path", "POST")
    }

    /** The frame the panel shows, as rows of pixels (true: ink), its number, and whether a
     * newer one is waiting for the panel. */
    fun screen(): Screen {
        val response = send("/api/dev/screen")
        val number = response.header("X-Frame")?.toIntOrNull()
            ?: throw IOException("the answer is not a frame: is the firmware built with APP_DEV_CONSOLE?")
        return Screen(Bmp.rows(response.body), number, response.header("X-Pending") == "1")
    }

    /** The picture once the panel has caught up with what was asked of it: a refresh after
     * `seenFrame`, waited for briefly, and then for as long as the device says a newer frame
     * is on its way. */
    fun settledScreen(seenFrame: Int): Screen {
        val started = System.currentTimeMillis()
        while (true) {
            val screen = screen()
            val waited = System.currentTimeMillis() - started
            if ((screen.isPending && waited < PENDING_FRAME_WAIT_MS) ||
                (screen.number == seenFrame && waited < NEW_FRAME_WAIT_MS)) {
                Thread.sleep(POLL_MS)
            } else {
                return screen
            }
        }
    }

    /** A value into an application's store, as its own kv_set would put it; the application
     * is told as when the web page saves its settings. */
    fun set(application: String, key: String, value: String) {
        send("/api/dev/kv?app=${encode(application)}&key=${encode(key)}", "POST", value.toByteArray())
    }

    /** Puts one file of an application's package on the device. */
    fun uploadAppFile(application: String, file: String, bytes: ByteArray) {
        send("/api/dev/app?name=${encode(application)}&file=${encode(file)}", "POST", bytes, UPLOAD_TIMEOUT_MS)
    }

    /** Restarts the device, the application to start into named. */
    fun restartInto(application: String) {
        send("/api/dev/run?app=${encode(application)}", "POST")
    }

    /** After a restart: the device is gone for a moment, then answers again. Returns its
     * status; cancelled, throws InterruptedException. */
    fun waitUntilBack(isCancelled: () -> Boolean): String {
        Thread.sleep(3_000)
        val deadline = System.currentTimeMillis() + RESTART_WAIT_MS
        while (System.currentTimeMillis() < deadline) {
            if (isCancelled()) throw InterruptedException("cancelled")
            try {
                return status()
            } catch (error: IOException) {
                Thread.sleep(1_000)
            }
        }
        throw IOException("the device did not come back in ${RESTART_WAIT_MS / 1000} seconds")
    }

    private fun encode(text: String): String = URLEncoder.encode(text, StandardCharsets.UTF_8)

    /** Uploads once, then waits for the exact image to finish probation. */
    fun flash(image: ByteArray): String = flash(image) { false }

    fun flash(image: ByteArray, isCancelled: () -> Boolean): String {
        fun checkCancelled() {
            if (isAborted || isCancelled()) throw InterruptedException("cancelled")
        }
        fun state(): Map<*, *> {
            checkCancelled()
            val text = String(send("/api/status", timeoutMs = 4_000).body, StandardCharsets.UTF_8)
            return try {
                Json.parse(text) as? Map<*, *> ?: throw IOException("invalid firmware status")
            } catch (error: Json.Error) {
                throw IOException("invalid firmware status", error)
            }
        }
        val before = state()
        if (before["ota_protocol"] == null && before["mode"] != "recovery") {
            return String(send("/api/dev/flash", "POST", image, 180_000).body, StandardCharsets.UTF_8)
        }
        return FirmwareUpdate(::state, { path, body, timeout ->
            firmwarePost(path, body, timeout, isCancelled)
        }, {
            checkCancelled()
            Thread.sleep(1_000)
        }).install(image, before)
    }

    /* HttpURLConnection's read timeout does not bound writing a stalled request body. */
    internal fun firmwarePost(path: String, body: ByteArray?, timeoutMs: Int, isCancelled: () -> Boolean) {
        if (isAborted || isCancelled()) throw InterruptedException("cancelled")
        val publisher = if (body == null) HttpRequest.BodyPublishers.noBody()
                        else HttpRequest.BodyPublishers.ofByteArray(body)
        val request = HttpRequest.newBuilder(URI.create(base + path))
            .timeout(Duration.ofMillis(timeoutMs.toLong()))
            .header("Content-Type", "application/octet-stream").POST(publisher).build()
        // Main and recovery share an address, but a pooled connection belongs to one boot.
        // Close each OTA client's pool before the next operation crosses that boundary.
        val client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(3))
            .version(HttpClient.Version.HTTP_1_1).build()
        val pending = client.sendAsync(request, HttpResponse.BodyHandlers.ofByteArray())
        firmwareRequest = pending
        val deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs.toLong())
        try {
            while (true) {
                if (isAborted || isCancelled()) throw InterruptedException("cancelled")
                if (System.nanoTime() >= deadline) throw IOException("firmware request timed out")
                val response = try { pending.get(250, TimeUnit.MILLISECONDS) }
                    catch (_: TimeoutException) { continue }
                    catch (_: CancellationException) { throw InterruptedException("cancelled") }
                    catch (error: ExecutionException) {
                        throw IOException("firmware connection interrupted", error.cause)
                    }
                if (response.statusCode() !in 200..299) {
                    val said = String(response.body(), StandardCharsets.UTF_8).take(200)
                    throw FirmwareUpdate.Rejected("HTTP ${response.statusCode()} from $path: $said")
                }
                return
            }
        } finally {
            pending.cancel(true)
            firmwareRequest = null
            client.close()
        }
    }

    companion object {
        private const val BUSY_RETRIES = 3
        private const val UPLOAD_TIMEOUT_MS = 120_000
        private const val RESTART_WAIT_MS = 60_000L
        private const val POLL_MS = 300L
        // A press that changes the picture shows within this; one that changes nothing is not
        // waited for any longer.
        private const val NEW_FRAME_WAIT_MS = 4_000L
        // A frame may be held back by the refresh policy for up to a minute.
        private const val PENDING_FRAME_WAIT_MS = 70_000L
    }
}
