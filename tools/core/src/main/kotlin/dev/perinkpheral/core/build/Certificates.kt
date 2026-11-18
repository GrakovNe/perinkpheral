// SPDX-License-Identifier: MIT
package dev.perinkpheral.core.build

import dev.perinkpheral.core.BuildError
import java.io.ByteArrayInputStream
import java.io.File
import java.security.cert.CertificateException
import java.security.cert.CertificateExpiredException
import java.security.cert.CertificateFactory
import java.security.cert.CertificateNotYetValidException
import java.security.cert.X509Certificate
import javax.naming.ldap.LdapName

/**
 * The certificate authorities an application trusts beside the browsers' set ("certificates"
 * in its manifest): files of its package, each a root in PEM or DER, trusted for its own
 * requests and nobody else's. The build holds them to that; the catalog names them, so that
 * the page can ask before it installs.
 */
object Certificates {
    /** The certificate in a file: PEM or DER, one of an authority, valid today. */
    fun read(file: File, most: Long): X509Certificate {
        if (!file.isFile) throw BuildError("${file.path}: the manifest names it under \"certificates\" and it is not there")
        if (file.length() > most) throw BuildError("${file.path}: ${file.length()} bytes, and a certificate may have $most")
        val certificate = try {
            CertificateFactory.getInstance("X.509").generateCertificate(ByteArrayInputStream(file.readBytes())) as X509Certificate
        } catch (error: CertificateException) {
            throw BuildError("${file.path}: not a certificate in PEM or DER")
        }
        if (certificate.basicConstraints < 0) {
            throw BuildError("${file.path}: ${authority(certificate)} is not a certificate authority: an application " +
                "trusts authorities, not servers")
        }
        try {
            certificate.checkValidity()
        } catch (error: CertificateExpiredException) {
            throw BuildError("${file.path}: ${authority(certificate)} expired on ${certificate.notAfter}")
        } catch (error: CertificateNotYetValidException) {
            throw BuildError("${file.path}: ${authority(certificate)} is not valid before ${certificate.notBefore}")
        }
        return certificate
    }

    /** What a person calls it: the CN of its subject, or the whole subject without one. */
    fun authority(certificate: X509Certificate): String {
        val subject = certificate.subjectX500Principal.name
        return try {
            LdapName(subject).rdns.lastOrNull { it.type.equals("CN", ignoreCase = true) }?.value?.toString() ?: subject
        } catch (error: Exception) {
            subject
        }
    }
}
