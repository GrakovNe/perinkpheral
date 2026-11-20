// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.simulator

import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CyclicBarrier
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

object InOrderTest {
    private const val PATIENCE_S = 20L

    /** How many are at work at once, and the most there ever were. */
    private class Gauge {
        private val now = AtomicInteger()
        val most = AtomicInteger()
        val atWork: Int get() = now.get()

        fun <R> during(work: () -> R): R {
            most.accumulateAndGet(now.incrementAndGet()) { seen, current -> maxOf(seen, current) }
            try {
                return work()
            } finally {
                now.decrementAndGet()
            }
        }
    }

    @JvmStatic fun main(args: Array<String>) {
        toldInTheItemsOrder()
        boundedByTheWorkers()
        aFailureStopsNoOther()
        anItemAloneRunsAlone()
        oneWorkerIsAPlainLoop()
        anErrorIsNotAResult()
        println("in order: order, bound, failures, alone, one worker and errors passed.")
    }

    /** The first item finishes last, and is still told first. */
    private fun toldInTheItemsOrder() {
        val items = (0 until 6).toList()
        val finished = Collections.synchronizedList(ArrayList<Int>())
        val othersDone = CountDownLatch(items.size - 1)
        val told = ArrayList<Int>()
        val caller = Thread.currentThread()
        InOrder.run(items, items.size, work = { item ->
            if (item == 0) check(othersDone.await(PATIENCE_S, TimeUnit.SECONDS)) { "the others did not run beside the first" }
            finished += item
            if (item != 0) othersDone.countDown()
            item * 10
        }) { item, result ->
            check(Thread.currentThread() === caller) { "told on the caller's thread" }
            check(result.getOrThrow() == item * 10)
            told += item
        }
        check(finished.last() == 0) { "the first item was to finish last: $finished" }
        check(told == items) { "told in the items' order, not as they finished: $told" }
    }

    /** As many at once as there are workers: all of them meet, and never one more. */
    private fun boundedByTheWorkers() {
        val workers = 4
        val gauge = Gauge()
        val meeting = CyclicBarrier(workers)
        var told = 0
        InOrder.run((0 until workers * 3).toList(), workers, work = {
            gauge.during { meeting.await(PATIENCE_S, TimeUnit.SECONDS) }
        }) { _, result ->
            result.getOrThrow()
            told++
        }
        check(told == workers * 3)
        check(gauge.most.get() == workers) { "${gauge.most.get()} at once with $workers workers" }
    }

    private fun aFailureStopsNoOther() {
        for (workers in listOf(1, 3)) {
            val told = ArrayList<String>()
            InOrder.run(listOf("a", "b", "c", "d"), workers, work = { item ->
                if (item == "b") throw IllegalStateException("b is broken")
                item.uppercase()
            }) { item, result ->
                told += item + "=" + result.fold({ it }, { "failed: ${it.message}" })
            }
            check(told == listOf("a=A", "b=failed: b is broken", "c=C", "d=D")) { "$workers workers: $told" }
        }
    }

    /** While an item that is alone runs, nothing else does, wherever it stands among the others. */
    private fun anItemAloneRunsAlone() {
        val gauge = Gauge()
        val besideAlone = AtomicInteger()
        val items = (0 until 12).toList()
        val told = ArrayList<Int>()
        InOrder.run(items, 4, isAlone = { it % 5 == 2 }, work = { item ->
            gauge.during {
                if (item % 5 == 2) {
                    // Long enough for a worker that should not be running to show itself.
                    repeat(20) {
                        besideAlone.accumulateAndGet(gauge.atWork) { seen, current -> maxOf(seen, current) }
                        Thread.sleep(5)
                    }
                } else {
                    Thread.sleep(30)
                }
            }
        }) { item, result ->
            result.getOrThrow()
            told += item
        }
        check(told == items)
        check(besideAlone.get() == 1) { "${besideAlone.get()} at work while one was to be alone" }
        check(gauge.most.get() > 1) { "the others were to run together" }
    }

    /** One worker: each item done and told before the next begins, on the caller's thread. */
    private fun oneWorkerIsAPlainLoop() {
        val caller = Thread.currentThread()
        val events = ArrayList<String>()
        InOrder.run(listOf(1, 2, 3), 1, isAlone = { it == 2 }, work = { item ->
            check(Thread.currentThread() === caller) { "one worker is the caller" }
            events += "work $item"
        }) { item, _ -> events += "told $item" }
        check(events == listOf("work 1", "told 1", "work 2", "told 2", "work 3", "told 3")) { "$events" }
        InOrder.run(emptyList<Int>(), 8, work = { it }) { _, _ -> error("nothing to tell") }
        check(runCatching { InOrder.run(listOf(1), 0, work = { it }) { _, _ -> } }.isFailure) { "no workers is refused" }
    }

    /** What is not the work's failure - the tools' own - ends the run instead of being told. */
    private fun anErrorIsNotAResult() {
        for (workers in listOf(1, 2)) {
            val thrown = runCatching {
                InOrder.run(listOf(1, 2), workers, work = { item -> if (item == 2) throw StackOverflowError("deep") }) { _, _ -> }
            }.exceptionOrNull()
            check(thrown is StackOverflowError) { "$workers workers: $thrown" }
        }
    }
}
