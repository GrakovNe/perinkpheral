// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.locks.ReentrantReadWriteLock

/**
 * Work done several at a time and told in the order it was given: what a person reads, and
 * what two runs are compared by, does not depend on which finished first.
 */
object InOrder {
    /**
     * Does `work` for every item on at most `workers` threads and calls `report` for each, on
     * the caller's thread, in the items' order, as soon as the item and all before it are done.
     * An item that throws is reported with its exception and stops no other. An item that
     * `isAlone` runs while no other does. One worker is a plain loop on the caller's thread.
     */
    fun <T, R> run(items: List<T>, workers: Int, isAlone: (T) -> Boolean = { false }, work: (T) -> R,
                   report: (item: T, result: Result<R>) -> Unit) {
        require(workers >= 1) { "at least one worker" }
        if (workers == 1 || items.size <= 1) {
            for (item in items) report(item, attempt(item, work))
            return
        }
        // Fair: an item that runs alone waits for those running and is not overtaken forever.
        val alone = ReentrantReadWriteLock(true)
        val pool = Executors.newFixedThreadPool(minOf(workers, items.size)) { task ->
            Thread(task, "perINKpheral worker").apply { isDaemon = true }
        }
        try {
            val results: List<Future<Result<R>>> = items.map { item ->
                pool.submit<Result<R>> {
                    val lock = if (isAlone(item)) alone.writeLock() else alone.readLock()
                    lock.lock()
                    try {
                        attempt(item, work)
                    } finally {
                        lock.unlock()
                    }
                }
            }
            for ((index, result) in results.withIndex()) {
                val got = try {
                    result.get()
                } catch (error: ExecutionException) {
                    // Not an Exception of the work's: out of memory, a broken build of the tools.
                    throw error.cause ?: error
                }
                report(items[index], got)
            }
        } finally {
            pool.shutdownNow()
        }
    }

    private fun <T, R> attempt(item: T, work: (T) -> R): Result<R> = try {
        Result.success(work(item))
    } catch (error: Exception) {
        Result.failure(error)
    }
}
