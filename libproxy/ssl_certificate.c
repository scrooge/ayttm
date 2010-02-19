/*
 * Ayttm 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#ifdef HAVE_OPENSSL
#define OPENSSL_NO_KRB5 1
#include <openssl/ssl.h>
#include <glib.h>
#include "ssl_certificate.h"
#ifndef __MINGW32__
#include <netdb.h>
#else
#include <winsock2.h>
#endif
#include <glib.h>
#include "intl.h"
#include "debug.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "globals.h"
#include "common.h"
#include "networking.h"

static SSLCertificate *ssl_certificate_new_lookup(X509 *x509_cert,
	const char *host, int port, int lookup);

static char *get_fqdn(const char *host, int port)
{
	struct addrinfo *hp;
	char *ret = NULL;

	if (host == NULL || strlen(host) == 0)
		return g_strdup("");

	hp = lookup_address(host, port, AF_UNSPEC);
	if (hp == NULL)
		ret = g_strdup(host);	/*caller should free */
	else
		ret = g_strdup(hp->ai_canonname);

	freeaddrinfo(hp);

	return ret;
}

char *readable_fingerprint(unsigned char *src, int len)
{
	int i = 0;
	char *ret;

	if (src == NULL)
		return NULL;
	ret = g_strdup("");
	while (i < len) {
		char *tmp2;
		if (i > 0)
			tmp2 = g_strdup_printf("%s:%02X", ret, src[i]);
		else
			tmp2 = g_strdup_printf("%02X", src[i]);
		g_free(ret);
		ret = g_strdup(tmp2);
		g_free(tmp2);
		i++;
	}
	return ret;
}

SSLCertificate *ssl_certificate_new(X509 *x509_cert, const char *host, int port)
{
	return ssl_certificate_new_lookup(x509_cert, host, port, TRUE);
}

static SSLCertificate *ssl_certificate_new_lookup(X509 *x509_cert,
	const char *host, int port, int lookup)
{
	SSLCertificate *cert = g_new0(SSLCertificate, 1);

	if (host == NULL || x509_cert == NULL) {
		ssl_certificate_destroy(cert);
		return NULL;
	}
	cert->x509_cert = X509_dup(x509_cert);
	if (lookup)
		cert->host = get_fqdn(host, port);
	else
		cert->host = g_strdup(host);
	cert->port = port;
	return cert;
}

static int is_dir_exist(const char *dir)
{
	struct stat s;

	if (dir == NULL)
		return FALSE;

	if (stat(dir, &s) < 0) {
		return FALSE;
	}

	return (S_ISDIR(s.st_mode));
}

static void ssl_certificate_save(SSLCertificate *cert)
{
	char *file, *port;
	FILE *fp;

	file = g_strconcat(config_dir, G_DIR_SEPARATOR_S,
		"certs", G_DIR_SEPARATOR_S, NULL);

	if (!is_dir_exist(file))
		mkdir(file, S_IRWXU);

	g_free(file);

	port = g_strdup_printf("%d", cert->port);
	file = g_strconcat(config_dir, G_DIR_SEPARATOR_S,
		"certs", G_DIR_SEPARATOR_S,
		cert->host, ".", port, ".cert", NULL);

	g_free(port);
	fp = fopen(file, "wb");
	if (fp == NULL) {
		g_free(file);
		eb_debug(DBG_CORE, "Can't save certificate !\n");
		return;
	}
	i2d_X509_fp(fp, cert->x509_cert);
	g_free(file);
	fclose(fp);

}

char *ssl_certificate_to_string(SSLCertificate *cert)
{
	char *ret, buf[100];
	char *issuer_commonname, *issuer_location, *issuer_organization;
	char *subject_commonname, *subject_location, *subject_organization;
	char *fingerprint, *sig_status;
	unsigned int n;
	unsigned char md[EVP_MAX_MD_SIZE];

	/* issuer */
	if (X509_NAME_get_text_by_NID(X509_get_issuer_name(cert->x509_cert),
			NID_commonName, buf, 100) >= 0)
		issuer_commonname = g_strdup(buf);
	else
		issuer_commonname = g_strdup(_("(Unspecified)"));
	if (X509_NAME_get_text_by_NID(X509_get_issuer_name(cert->x509_cert),
			NID_localityName, buf, 100) >= 0) {
		issuer_location = g_strdup(buf);
		if (X509_NAME_get_text_by_NID(X509_get_issuer_name(cert->
					x509_cert), NID_countryName, buf,
				100) >= 0)
			issuer_location =
				g_strconcat(issuer_location, ", ", buf, NULL);
	} else if (X509_NAME_get_text_by_NID(X509_get_issuer_name(cert->
				x509_cert), NID_countryName, buf, 100) >= 0)
		issuer_location = g_strdup(buf);
	else
		issuer_location = g_strdup(_("(Unspecified)"));

	if (X509_NAME_get_text_by_NID(X509_get_issuer_name(cert->x509_cert),
			NID_organizationName, buf, 100) >= 0)
		issuer_organization = g_strdup(buf);
	else
		issuer_organization = g_strdup(_("(Unspecified)"));

	/* subject */
	if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert->x509_cert),
			NID_commonName, buf, 100) >= 0)
		subject_commonname = g_strdup(buf);
	else
		subject_commonname = g_strdup(_("(Unspecified)"));
	if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert->x509_cert),
			NID_localityName, buf, 100) >= 0) {
		subject_location = g_strdup(buf);
		if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert->
					x509_cert), NID_countryName, buf,
				100) >= 0)
			subject_location =
				g_strconcat(subject_location, ", ", buf, NULL);
	} else if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert->
				x509_cert), NID_countryName, buf, 100) >= 0)
		subject_location = g_strdup(buf);
	else
		subject_location = g_strdup(_("(Unspecified)"));

	if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert->x509_cert),
			NID_organizationName, buf, 100) >= 0)
		subject_organization = g_strdup(buf);
	else
		subject_organization = g_strdup(_("(Unspecified)"));

	/* fingerprint */
	X509_digest(cert->x509_cert, EVP_md5(), md, &n);
	fingerprint = readable_fingerprint(md, (int)n);

	/* signature */
	sig_status = ssl_certificate_check_signer(cert->x509_cert);

	ret = g_strdup_printf(_("  <b>Certificate Owner:</b> %s (%s) in %s\n  "
			"<b>Signed by:</b> %s (%s) in %s\n  "
			"<b>Fingerprint:</b> %s\n  "
			"<b>Signature status:</b> %s"),
		subject_commonname, subject_organization, subject_location,
		issuer_commonname, issuer_organization, issuer_location,
		fingerprint, (sig_status == NULL ? "correct" : sig_status));

	if (issuer_commonname)
		g_free(issuer_commonname);
	if (issuer_location)
		g_free(issuer_location);
	if (issuer_organization)
		g_free(issuer_organization);
	if (subject_commonname)
		g_free(subject_commonname);
	if (subject_location)
		g_free(subject_location);
	if (subject_organization)
		g_free(subject_organization);
	if (fingerprint)
		g_free(fingerprint);
	if (sig_status)
		g_free(sig_status);
	return ret;
}

void ssl_certificate_destroy(SSLCertificate *cert)
{
	if (cert == NULL)
		return;

	if (cert->x509_cert)
		X509_free(cert->x509_cert);
	if (cert->host)
		g_free(cert->host);
	g_free(cert);
	cert = NULL;
}

void ssl_certificate_delete_from_disk(SSLCertificate *cert)
{
	char *buf;
	char *file;
	buf = g_strdup_printf("%d", cert->port);
	file = g_strconcat(config_dir, G_DIR_SEPARATOR_S,
		"certs", G_DIR_SEPARATOR_S,
		cert->host, ".", buf, ".cert", NULL);
	unlink(file);
	g_free(buf);
	g_free(file);
}

SSLCertificate *ssl_certificate_find(const char *host, int port)
{
	return ssl_certificate_find_lookup(host, port, TRUE);
}

SSLCertificate *ssl_certificate_find_lookup(const char *host, int port,
	int lookup)
{
	char *file = NULL;
	char *buf = NULL;
	char *fqdn_host = NULL;
	SSLCertificate *cert = NULL;
	X509 *tmp_x509 = NULL;
	FILE *fp = NULL;

	if (lookup)
		fqdn_host = get_fqdn(host, port);
	else
		fqdn_host = g_strdup(host);

	buf = g_strdup_printf("%d", port);
	file = g_strconcat(config_dir, G_DIR_SEPARATOR_S,
		"certs", G_DIR_SEPARATOR_S, fqdn_host, ".", buf, ".cert", NULL);

	g_free(buf);
	fp = fopen(file, "rb");
	if (fp == NULL) {
		g_free(file);
		g_free(fqdn_host);
		return NULL;
	}

	if ((tmp_x509 = d2i_X509_fp(fp, 0)) != NULL) {
		cert = ssl_certificate_new_lookup(tmp_x509, fqdn_host, port,
			lookup);
		X509_free(tmp_x509);
	}
	fclose(fp);
	g_free(file);
	g_free(fqdn_host);

	return cert;
}

static int ssl_certificate_compare(SSLCertificate *cert_a,
	SSLCertificate *cert_b)
{
	if (cert_a == NULL || cert_b == NULL)
		return FALSE;
	else if (!X509_cmp(cert_a->x509_cert, cert_b->x509_cert))
		return TRUE;
	else
		return FALSE;
}

char *ssl_certificate_check_signer(X509 *cert)
{
	X509_STORE_CTX store_ctx;
	X509_STORE *store;
	char *err_msg = NULL;

	store = X509_STORE_new();
	if (store == NULL) {
		printf("Can't create X509_STORE\n");
		return NULL;
	}
	if (!X509_STORE_set_default_paths(store)) {
		X509_STORE_free(store);
		return g_strdup(_("Can't load X509 default paths"));
	}

	X509_STORE_CTX_init(&store_ctx, store, cert, NULL);

	if (!X509_verify_cert(&store_ctx)) {
		err_msg =
			g_strdup(X509_verify_cert_error_string
			(X509_STORE_CTX_get_error(&store_ctx)));
		eb_debug(DBG_CORE, "Can't check signer: %s\n", err_msg);
		X509_STORE_CTX_cleanup(&store_ctx);
		X509_STORE_free(store);
		return err_msg;

	}
	X509_STORE_CTX_cleanup(&store_ctx);
	X509_STORE_free(store);
	return NULL;
}

int ssl_certificate_check(X509 *x509_cert, const char *host, int port,
	void *data)
{
	SSLCertificate *current_cert =
		ssl_certificate_new(x509_cert, host, port);
	SSLCertificate *known_cert;

	if (current_cert == NULL) {
		eb_debug(DBG_CORE, "Buggy certificate !\n");
		return FALSE;
	}

	eb_debug(DBG_CORE, "%s%d\n", host, port);
	known_cert = ssl_certificate_find(host, port);

	if (known_cert == NULL) {
		char *err_msg, *cur_cert_str, *sig_status;
		int result = 0;

		sig_status = ssl_certificate_check_signer(x509_cert);

		if (sig_status == NULL) {
			char buf[1024];
			if (X509_NAME_get_text_by_NID(X509_get_subject_name
					(x509_cert), NID_commonName, buf,
					100) >= 0)
				if (!strcmp(buf, current_cert->host)) {
					ssl_certificate_save(current_cert);
					ssl_certificate_destroy(current_cert);
					return TRUE;
				}
		} else
			g_free(sig_status);

		cur_cert_str = ssl_certificate_to_string(current_cert);

		err_msg =
			g_strdup_printf(_
			("The server <b>%s</b> presented an unknown SSL certificate:\n\n%s\n\n"
				"Do you want to continue connecting?"),
			current_cert->host, cur_cert_str);

		result = ay_connection_verify(err_msg,
			_("Unknown Certificate!"), data);

		g_free(cur_cert_str);

		g_free(err_msg);

		if (result) {
			ssl_certificate_save(current_cert);
		}
		ssl_certificate_destroy(current_cert);

		return result;
	} else if (!ssl_certificate_compare(current_cert, known_cert)) {
		char *err_msg, *known_cert_str, *cur_cert_str;
		int result = -1;

		known_cert_str = ssl_certificate_to_string(known_cert);
		cur_cert_str = ssl_certificate_to_string(current_cert);
		err_msg =
			g_strdup_printf(_
			("%s's SSL certificate changed!\nWe have saved this one:\n%s\n\nIt is now:\n%s\n\n"
				"This could mean the server answering is not the known one.\n"
				"Do you want to continue connecting ?"),
			current_cert->host, known_cert_str, cur_cert_str);
		g_free(cur_cert_str);
		g_free(known_cert_str);

		result = ay_connection_verify(err_msg,
			_("Changed Certificate!"), data);

		g_free(err_msg);

		if (result) {
			ssl_certificate_save(current_cert);
		}
		ssl_certificate_destroy(current_cert);

		return result;
	}

	ssl_certificate_destroy(current_cert);
	ssl_certificate_destroy(known_cert);
	return TRUE;
}

#endif				/* USE_OPENSSL */
