// SPDX-License-Identifier: MIT
package org.perinkpheral.core.device

import org.perinkpheral.core.Json
import java.io.IOException

/**
 * The device's log as it grows, across restarts. A device that was away and answers again is
 * asked when it started (now less its uptime): started anew, its log is read from the
 * beginning, which a position alone does not tell once the new log has grown past the old one.
 */
class LogFollower(private val client: DeviceClient) {
    /** What was written since the last call; `restarted` when the device started anew in
     * between, the text then being its new log from the beginning. */
    class Part(val text: String, val restarted: Boolean)

    private var end: Long? = null
    private var startedAt: Long? = null
    private var wasAway = true

    /** Throws IOException while the device does not answer. */
    fun next(): Part {
        try {
            var restarted = false
            if (wasAway) {
                val started = startedAt()
                restarted = startedAt.let { it != null && started - it > RESTART_MARGIN_MS }
                startedAt = started
                if (restarted) end = null
                wasAway = false
            }
            var part = client.log(end)
            if (end != null && part.end < end!!) {
                restarted = true
                part = client.log(null)
            }
            end = part.end
            return Part(part.text, restarted)
        } catch (error: IOException) {
            wasAway = true
            throw error
        }
    }

    private fun startedAt(): Long {
        val status = Json.parse(client.status()) as? Map<*, *>
        val uptime = status?.get("uptime_s") as? Long ?: throw IOException("the device's status has no uptime")
        return System.currentTimeMillis() - uptime * 1000
    }

    private companion object {
        // Uptime is in whole seconds, and the answer takes its time.
        const val RESTART_MARGIN_MS = 5_000L
    }
}
