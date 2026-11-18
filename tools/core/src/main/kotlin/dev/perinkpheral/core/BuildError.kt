// SPDX-License-Identifier: MIT
package dev.perinkpheral.core

/** What went wrong with an application, a catalog or a command, in words a person reads: the
 * build's, the checks' and the command line's. Whoever catches it shows the message alone. */
class BuildError(message: String) : Exception(message)
