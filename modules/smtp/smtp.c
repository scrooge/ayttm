/*
 * smtp.c
 */

/*
 * SMTP Extension for everybuddy 
 *
 * Copyright (C) 2002, Philip Tellis <philip.tellis@iname.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "intl.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "service.h"
#include "plugin_api.h"

#include "input_list.h"
#include "value_pair.h"
#include "util.h"
#include "libproxy/networking.h"

#include "message_parse.h"	/* eb_parse_incoming_message */
#include "status.h"		/* buddy_login,  */
				/* buddy_logoff,  */
				/* buddy_update_status */
#include "messages.h"

/*******************************************************************************
 *                             Begin Module Code
 ******************************************************************************/
/*  Module defines */
#ifndef USE_POSIX_DLOPEN
#define plugin_info smtp_LTX_plugin_info
#define SERVICE_INFO smtp_LTX_SERVICE_INFO
#define module_version smtp_LTX_module_version
#endif

/* Function Prototypes */
static int plugin_init();
static int plugin_finish();
struct service_callbacks *query_callbacks();

static int is_setting_state = 0;

static int ref_count = 0;

static int do_smtp_debug = 0;
static int default_online = 0;

/*  Module Exports */
PLUGIN_INFO plugin_info = {
	PLUGIN_SERVICE,
	"SMTP",
	"Provides Simple Mail Transfer Protocol (SMTP) support",
	"$Revision: 1.25 $",
	"$Date: 2009/09/17 12:04:59 $",
	&ref_count,
	plugin_init,
	plugin_finish,
	NULL
};

struct service SERVICE_INFO = {
	"SMTP",
	-1,
	SERVICE_CAN_OFFLINEMSG |	/* all messages are offline */
		SERVICE_CAN_FILETRANSFER,	/* true so i can prevent file 
						   transfer altogether */
	NULL
};

/* End Module Exports */

unsigned int module_version()
{
	return CORE_VERSION;
}

static int plugin_init()
{
	input_list *il = calloc(1, sizeof(input_list));
	ref_count = 0;

	plugin_info.prefs = il;
	il->widget.checkbox.value = &do_smtp_debug;
	il->name = "do_smtp_debug";
	il->label = _("Enable debugging");
	il->type = EB_INPUT_CHECKBOX;

	il->next = calloc(1, sizeof(input_list));
	il = il->next;
	il->widget.checkbox.value = &default_online;
	il->name = "default_online";
	il->label = _("Contacts online by default");
	il->type = EB_INPUT_CHECKBOX;

	return (0);
}

static int plugin_finish()
{
	eb_debug(DBG_MOD, "Returning the ref_count: %i\n", ref_count);
	return (ref_count);
}

/*******************************************************************************
 *                             End Module Code
 ******************************************************************************/

#ifdef __STDC__
int SMTP_DEBUGLOG(char *fmt, ...)
#else
int SMTP_DEBUGLOG(fmt, va_alist)
char *fmt;
va_dcl
#endif
{
	va_list ap;

#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif

	vfprintf(stderr, fmt, ap);
	fflush(stderr);
	va_end(ap);
	return 0;
}

#define LOG(x) if(do_smtp_debug) { SMTP_DEBUGLOG("%s:%d: ", __FILE__, __LINE__); \
	SMTP_DEBUGLOG x; \
	SMTP_DEBUGLOG("\n"); }

#define WARNING(x) if(do_smtp_debug) { SMTP_DEBUGLOG("%s:%d: warning: ", __FILE__, __LINE__); \
	SMTP_DEBUGLOG x; \
	SMTP_DEBUGLOG("\n"); }

#define SMTP_MSG_COLOR	"#2080d0"
static char *eb_smtp_get_color(void)
{
	return SMTP_MSG_COLOR;
}

enum smtp_status_code { SMTP_STATUS_ONLINE, SMTP_STATUS_OFFLINE };

typedef struct eb_smtp_account_data {
	int status;		/* always online */
} eb_smtp_account_data;

typedef struct eb_smtp_local_account_data {
	char password[MAX_PREF_LEN];	/* in case of SMTP Auth? */
	int status;		/* always online */
	char smtp_host[MAX_PREF_LEN];
	char smtp_port[MAX_PREF_LEN];
} eb_smtp_local_account_data;

static void smtp_account_prefs_init(eb_local_account *ela)
{
	eb_smtp_local_account_data *sla = ela->protocol_local_account_data;

	input_list *il = calloc(1, sizeof(input_list));

	ela->prefs = il;
	il->widget.entry.value = ela->handle;
	il->name = "SCREEN_NAME";
	il->label = _("_Email Address:");
	il->type = EB_INPUT_ENTRY;

	il->next = calloc(1, sizeof(input_list));
	il = il->next;
	il->widget.entry.value = sla->password;
	il->name = "PASSWORD";
	il->label = _("_Password:");
	il->type = EB_INPUT_ENTRY;

	il->next = calloc(1, sizeof(input_list));
	il = il->next;
	il->widget.entry.value = sla->smtp_host;
	il->name = "smtp_host";
	il->label = _("SMTP _Server:");
	il->type = EB_INPUT_ENTRY;

	il->next = calloc(1, sizeof(input_list));
	il = il->next;
	il->widget.entry.value = sla->smtp_port;
	il->name = "smtp_port";
	il->label = _("P_ort:");
	il->type = EB_INPUT_ENTRY;

}

static LList *eb_smtp_buddies = NULL;

static int smtp_tcp_readline(char *buff, int maxlen, AyConnection *fd)
{
	int n, rc;
	char c;

	for (n = 1; n < maxlen; n++) {
		do
			rc = ay_connection_read(fd, &c, 1);
		while(rc == -1 && errno == EINTR);

		if (rc == 1) {
			if(c == '\r')
				continue;
			*buff = c;
			if (c == '\n')
				break;
			buff++;
		} else if (rc == 0) {
			if (n == 1)
				return (0);
			else
				break;
		} else
			return -1;
	}

	*buff = 0;
	return (n);
}

static int smtp_tcp_writeline(char *buff, AyConnection *fd)
{
	int len = strlen(buff);
	int i;
	int ret = 0;
	for (i = 1; i <= 2; i++)
		if (buff[len - i] == '\r' || buff[len - i] == '\n')
			buff[len - i] = '\0';

	ret = ay_connection_write(fd, buff, strlen(buff));
	ret += ay_connection_write(fd, "\r\n", 2);

	return ret;
}

static eb_local_account *eb_smtp_read_local_account_config(LList *pairs)
{
	eb_local_account *ela;
	eb_smtp_local_account_data *sla;

	if (!pairs) {
		WARNING(("eb_smtp_read_local_account_config: pairs == NULL"));
		return NULL;
	}

	ela = calloc(1, sizeof(eb_local_account));
	sla = calloc(1, sizeof(eb_smtp_local_account_data));

	sla->status = SMTP_STATUS_OFFLINE;

	ela->service_id = SERVICE_INFO.protocol_id;
	ela->protocol_local_account_data = sla;

	smtp_account_prefs_init(ela);
	eb_update_from_value_pair(ela->prefs, pairs);

	if (!sla->smtp_host[0])
		strncpy(sla->smtp_host, "127.0.0.1", sizeof(sla->smtp_host));
	if (!sla->smtp_port[0])
		strncpy(sla->smtp_port, "25", sizeof(sla->smtp_host));

	return ela;
}

static LList *eb_smtp_write_local_config(eb_local_account *account)
{
	return eb_input_to_value_pair(account->prefs);
}

static void _buddy_change_state(void *data, void *user_data)
{
	eb_account *ea = find_account_by_handle(data, SERVICE_INFO.protocol_id);
	eb_smtp_account_data *sad;
	int status = (int)user_data;

	if (!ea)
		return;

	sad = ea->protocol_account_data;

	sad->status = status;

	if (status == SMTP_STATUS_ONLINE)
		buddy_login(ea);
	else
		buddy_logoff(ea);

	buddy_update_status(ea);
}

static LList *pending_connects = NULL;

static void eb_smtp_login(eb_local_account *account)
{
	/* we should always be logged in */
	eb_smtp_local_account_data *sla = account->protocol_local_account_data;
	enum smtp_status_code status = SMTP_STATUS_OFFLINE;

	if (account->status_menu) {
		sla->status = SMTP_STATUS_ONLINE;
		is_setting_state = 1;
		eb_set_active_menu_status(account->status_menu,
			SMTP_STATUS_ONLINE);
		is_setting_state = 0;
	}
	account->connected = 1;
	ref_count++;

	if (default_online)
		status = SMTP_STATUS_ONLINE;

	l_list_foreach(eb_smtp_buddies, _buddy_change_state, (void *)status);
}

static void eb_smtp_logout(eb_local_account *account)
{
	/* cannot logout */
	eb_smtp_local_account_data *sla = account->protocol_local_account_data;
	LList *l;

	for (l = pending_connects; l; l = l->next)
		ay_connection_cancel_connect((int)l->data);

	account->connected = 0;
	ref_count--;
	if (account->status_menu) {
		sla->status = SMTP_STATUS_OFFLINE;
		is_setting_state = 1;
		eb_set_active_menu_status(account->status_menu,
			SMTP_STATUS_OFFLINE);
		is_setting_state = 0;
	}

	l_list_foreach(eb_smtp_buddies, _buddy_change_state,
		(void *)SMTP_STATUS_OFFLINE);
}

static LList *eb_smtp_get_states()
{
	LList *states = NULL;

	states = l_list_append(states, "Online");
	states = l_list_append(states, "Offline");

	return states;
}

static int eb_smtp_get_current_state(eb_local_account *account)
{
	eb_smtp_local_account_data *sla = account->protocol_local_account_data;

	return sla->status;
}

static void eb_smtp_set_current_state(eb_local_account *account, int state)
{
	eb_smtp_local_account_data *sla = account->protocol_local_account_data;

	if (is_setting_state)
		return;

	if (sla->status == SMTP_STATUS_OFFLINE && state == SMTP_STATUS_ONLINE)
		eb_smtp_login(account);
	else if (sla->status == SMTP_STATUS_ONLINE
		&& state == SMTP_STATUS_OFFLINE)
		eb_smtp_logout(account);

	sla->status = state;
}

static void eb_smtp_set_idle(eb_local_account *account, int idle)
{
}

static void eb_smtp_set_away(eb_local_account *account, char *message, int away)
{
}

static eb_account *eb_smtp_new_account(eb_local_account *ela,
	const char *account)
{
	eb_account *ea = calloc(1, sizeof(eb_account));
	eb_smtp_account_data *sad = calloc(1, sizeof(eb_smtp_account_data));

	ea->protocol_account_data = sad;
	ea->ela = ela;
	strncpy(ea->handle, account, 255);
	ea->service_id = SERVICE_INFO.protocol_id;
	sad->status = SMTP_STATUS_OFFLINE;

	return ea;
}

static void eb_smtp_add_user(eb_account *account)
{
	eb_smtp_account_data *sad = account->protocol_account_data;
	eb_local_account *ela = find_local_account_for_remote(account, 0);
	eb_smtp_local_account_data *sla;

	if (!ela) {
		WARNING(("eb_smtp_add_user: ela == NULL"));
		return;
	}

	sla = ela->protocol_local_account_data;

	eb_smtp_buddies = l_list_append(eb_smtp_buddies, account->handle);

	if ((sad->status = sla->status) == SMTP_STATUS_ONLINE)
		buddy_login(account);
}

static void eb_smtp_del_user(eb_account *account)
{
	eb_smtp_buddies = l_list_remove(eb_smtp_buddies, account->handle);
}

static eb_account *eb_smtp_read_account_config(eb_account *ea, LList *config)
{
	eb_smtp_account_data *sad = calloc(1, sizeof(eb_smtp_account_data));

	sad->status = SMTP_STATUS_OFFLINE;

	ea->protocol_account_data = sad;

	eb_smtp_add_user(ea);

	return ea;
}

static int eb_smtp_query_connected(eb_account *account)
{
	eb_smtp_account_data *sad = account->protocol_account_data;

	return (sad->status == SMTP_STATUS_ONLINE);
}

static char *status_strings[] = {
	"",
	"Offline"
};

static const char *eb_smtp_get_status_string(eb_account *account)
{
	eb_smtp_account_data *sad = account->protocol_account_data;

	return status_strings[sad->status];
}

#include "smtp_online.xpm"
#include "smtp_away.xpm"

static const char **eb_smtp_get_status_pixmap(eb_account *account)
{
	eb_smtp_account_data *sad;

	sad = account->protocol_account_data;

	if (sad->status == SMTP_STATUS_ONLINE)
		return smtp_online_xpm;
	else
		return smtp_away_xpm;
}

static void eb_smtp_send_file(eb_local_account *from, eb_account *to,
	char *file)
{
	ay_do_info(_("SMTP Warning"),
		_("You cannot send files through SMTP... yet"));
}

static int validate_or_die_gracefully(const char *buff, const char *valid,
	AyConnection *fd)
{
	if (strstr(buff, valid) == buff) {
		return 1;
	}

	LOG(("Server responded: %s", buff));
	smtp_tcp_writeline("QUIT", fd);
	ay_connection_free(fd);
	return 0;
}

enum smtp_states {
	SMTP_CONNECT, SMTP_HELO,
	SMTP_FROM, SMTP_TO,
	SMTP_DATA, SMTP_DATA_END,
	SMTP_QUIT
};

struct smtp_callback_data {
	int tag;
	char localhost[255];
	eb_local_account *from;
	eb_account *to;
	char *msg;
	enum smtp_states state;
};

static const char *expected[7] = {
	"220", "250", "250", "250", "354", "250", "221"
};

static void destroy_callback_data(struct smtp_callback_data *d)
{
	if (d->tag)
		eb_input_remove(d->tag);
	free(d->msg);
	free(d);
}

static void smtp_message_sent(struct smtp_callback_data *d, int success)
{
	char reply[1024] = "<FONT COLOR=\"#a0a0a0\"><I>";
	if (success)
		strcat(reply, _("Message sent via SMTP."));
	else
		strcat(reply, _("Error sending message via SMTP."));
	strcat(reply, "</I></FONT>");

	eb_parse_incoming_message(d->from, d->to, reply);
}

static void send_message_async(AyConnection *fd, eb_input_condition cond, void *data)
{
	struct smtp_callback_data *d = data;
	char buff[1024];

	if (smtp_tcp_readline(buff, sizeof(buff) - 1, fd) <= 0) {
		WARNING(("smtp server closed connection"));
		ay_connection_free(fd);
		destroy_callback_data(d);
	};

	if (!validate_or_die_gracefully(buff, expected[d->state], fd)) {
		smtp_message_sent(d, 0);
		destroy_callback_data(d);
	}

	switch (d->state) {
	case SMTP_CONNECT:
		snprintf(buff, sizeof(buff) - 1, "HELO %s", d->localhost);
		d->state = SMTP_HELO;
		break;
	case SMTP_HELO:
		snprintf(buff, sizeof(buff) - 1, "MAIL FROM: <%s>",
			d->from->handle);
		d->state = SMTP_FROM;
		break;
	case SMTP_FROM:
		snprintf(buff, sizeof(buff) - 1, "RCPT TO: <%s>",
			d->to->handle);
		d->state = SMTP_TO;
		break;
	case SMTP_TO:
		strcpy(buff, "DATA");
		d->state = SMTP_DATA;
		break;
	case SMTP_DATA:
		{
			int n, len = strlen(d->msg);
			char buf[1024];
			/* avoid having no To: in mail - server may append 
			   "Undisclosed recipients" */
			snprintf(buf, 1024, "To: %s <%s>", d->to->handle,
				d->to->handle);
			smtp_tcp_writeline(buf, fd);
			for (n = 1;
				d->msg[len - n] == '\r'
				|| d->msg[len - n] == '\n'; n++)
				d->msg[len - n] = '\0';
			if (strncasecmp(d->msg, "Subject:", strlen("Subject:")))
				smtp_tcp_writeline("", fd);
			smtp_tcp_writeline(d->msg, fd);
			strcpy(buff, ".");
		}
		d->state = SMTP_DATA_END;
		break;
	case SMTP_DATA_END:
		strcpy(buff, "QUIT");
		d->state = SMTP_QUIT;
		break;
	case SMTP_QUIT:
		smtp_message_sent(d, 1);
		destroy_callback_data(d);
		return;
	}
	smtp_tcp_writeline(buff, fd);
}

static void eb_smtp_got_connected(AyConnection *fd, int error, void *data)
{
	struct smtp_callback_data *d = data;

	if (error) {
		if (error != AY_CANCEL_CONNECT)
			WARNING(("Could not connect to smtp server: %d: %s",
					error, strerror(error)));
		destroy_callback_data(d);
		return;
	}

	pending_connects = l_list_remove(pending_connects, (void *)d->tag);

	d->tag = ay_connection_input_add(fd, EB_INPUT_READ, send_message_async, d);
}

static int eb_smtp_send_im(eb_local_account *account_from,
	eb_account *account_to, char *message)
{
	AyConnection *fd;
	char localhost[255];
	struct smtp_callback_data *d;
	eb_smtp_local_account_data *sla =
		account_from->protocol_local_account_data;

	if (gethostname(localhost, sizeof(localhost) - 1) == -1) {
		strcpy(localhost, "localhost");
		WARNING(("could not get localhost name: %d: %s",
				errno, strerror(errno)));
		return 0;
	}

	fd = ay_connection_new(sla->smtp_host, atoi(sla->smtp_port),
		AY_CONNECTION_TYPE_PLAIN);

	d = calloc(1, sizeof(struct smtp_callback_data));
	strcpy(d->localhost, localhost);
	d->from = account_from;
	d->to = account_to;
	d->msg = strdup(message);
	d->tag = ay_connection_connect(fd, eb_smtp_got_connected, NULL, NULL, d);

	pending_connects = l_list_append(pending_connects, (void *)d->tag);

	return 1;
}

static char *eb_smtp_check_login(const char *user, const char *pass)
{
	if (strchr(user, '@') == NULL) {
		return strdup(_("SMTP logins must have @domain.tld part."));
	}
	return NULL;
}

struct service_callbacks *query_callbacks()
{
	struct service_callbacks *sc;

	sc = calloc(1, sizeof(struct service_callbacks));

	sc->query_connected = eb_smtp_query_connected;
	sc->login = eb_smtp_login;
	sc->logout = eb_smtp_logout;
	sc->check_login = eb_smtp_check_login;

	sc->send_im = eb_smtp_send_im;

	sc->read_local_account_config = eb_smtp_read_local_account_config;
	sc->write_local_config = eb_smtp_write_local_config;
	sc->read_account_config = eb_smtp_read_account_config;

	sc->get_states = eb_smtp_get_states;
	sc->get_current_state = eb_smtp_get_current_state;
	sc->set_current_state = eb_smtp_set_current_state;

	sc->new_account = eb_smtp_new_account;
	sc->add_user = eb_smtp_add_user;
	sc->del_user = eb_smtp_del_user;

	sc->get_status_string = eb_smtp_get_status_string;
	sc->get_status_pixmap = eb_smtp_get_status_pixmap;

	sc->set_idle = eb_smtp_set_idle;
	sc->set_away = eb_smtp_set_away;

	sc->send_file = eb_smtp_send_file;

	sc->get_color = eb_smtp_get_color;

	return sc;
}
