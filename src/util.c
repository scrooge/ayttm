/*
 * Ayttm 
 *
 * Copyright (C) 2003, the Ayttm team
 * 
 * Ayttm is derivative of Everybuddy
 * Copyright (C) 1999-2002, Torrey Searle <tsearle@uci.edu>
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

/*
 * util.c
 * just some handy functions to have around
 *
 */

#include "intl.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <assert.h>
#include <networking.h>

#ifdef __MINGW32__
#include <winsock2.h>
#else
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "util.h"
#include "status.h"
#include "globals.h"
#include "chat_window.h"
#include "value_pair.h"
#include "plugin.h"
#include "dialog.h"
#include "service.h"
#include "offline_queue_mgmt.h"
#include "add_contact_window.h"
#include "messages.h"
#include "activity_bar.h"
#include "smileys.h"

#ifndef NAME_MAX
#define NAME_MAX 4096
#endif

/* bug 929347 */
LList *nick_modify_utility = NULL;

int clean_pid(void *dummy)
{
#ifndef __MINGW32__
	int status;
	pid_t pid;

	pid = waitpid(-1, &status, WNOHANG);

	if (pid == 0)
		return TRUE;
#endif

	return FALSE;
}

char *get_local_addresses()
{
#ifndef __MINGW32__
	char command[] =
		"/sbin/ifconfig `netstat -nr | grep '^0\\.0' | tr -s ' ' ' ' | cut -f 8 -d' '` | grep inet | tr -s ' ' ':' | cut -f4 -d:";
#endif
	char buff[1024];
	static char addresses[1024];
	struct hostent *hn;
	FILE *f = NULL;

	addresses[0] = '\0';

	gethostname(buff, sizeof(buff));

	hn = gethostbyname(buff);
	if (hn) {
		char *quad = hn->h_addr_list[0];
		snprintf(addresses, sizeof(addresses), "%d.%d.%d.%d",
			quad[0], quad[1], quad[2], quad[3]);
#ifndef __MINGW32__
	}
	if ((!hn || !strcmp(addresses, "127.0.0.1"))
		&& (f = popen(command, "r")) != NULL) {
		int i = 0;

		do {
			int r = fgetc(f);
			buff[i] = (char)r;
			if (buff[i] == '\r' || buff[i] == '\n' || r == EOF)
				buff[i] = '\0';
			else if (i >= sizeof(buff) - 1) {
				buff[i] = '\0';
				/*return error? */
			}
		} while (buff[i++]);

		pclose(f);
		strncpy(addresses, buff, sizeof(addresses));
#endif
	} else {
		addresses[0] = 0;
	}
	return addresses;
}

/* a valid domain name is one that contains only alphanumeric, - and . 
 * if it has anything else, then it can't be a domain name
 */

static int is_valid_domain(char *name)
{
	int i;
	int dot_count = 0;
	if (name[0] == '-' || name[0] == '.')
		return FALSE;

	for (i = 0; name[i] && name[i] != '/' && name[i] != ':'; i++) {
		if (!isalnum(name[i]) && name[i] != '.' && name[i] != '-')
			return FALSE;

		if (name[i] == '.') {
			if (name[i - 1] == '.' || name[i - 1] == '-'
				|| name[i + 1] == '.' || name[i + 1] == '-')
				return FALSE;

			dot_count++;
		}
	}
	if (name[i] == ':') {
		for (i = i + 1; name[i] && name[i] != '/'; i++) {
			if (!isdigit(name[i]))
				return FALSE;
		}
	}
	return dot_count > 0;
}

char *escape_string(const char *input)
{
	GString *temp_result = g_string_sized_new(2048);
	char *result;
	int ipos = 0;
	if (!input)
		g_string_append(temp_result, "");

	for (ipos = 0; input && input[ipos]; ipos++) {
		if (input[ipos] == '\n')
			g_string_append(temp_result, "\\n");
		else if (input[ipos] == '\r')
			g_string_append(temp_result, "\\r");
		else if (input[ipos] == '\\')
			g_string_append(temp_result, "\\\\");
		else if (input[ipos] == '"')
			g_string_append(temp_result, "\\\"");
		else
			g_string_append_c(temp_result, input[ipos]);
	}

	result = temp_result->str;
	g_string_free(temp_result, FALSE);
	return result;
}

char *unescape_string(const char *input)
{
	char *result = NULL;
	int ipos = 0, opos = 0;

	if (!input)
		return strdup("");

	result = malloc((strlen(input) + 1) * sizeof(char));

	while (input && input[ipos]) {
		char c = input[ipos++];
		if (c == '\\') {
			c = input[ipos++];
			switch (c) {
			case 'n':
				result[opos++] = '\n';
				break;
			case 'r':
				result[opos++] = '\r';
				break;
			case '\\':
				result[opos++] = '\\';
				break;
			case '\"':
				result[opos++] = '\"';
				break;
			}
		} else
			result[opos++] = c;
	}
	result[opos] = '\0';

	result = realloc(result, opos + 1);

	return result;

}

/* this function will be used to determine if a given token is a link */

static int is_link(char *token)
{
	int i;
	int len = strlen(token);

	if (token[0] == '<')
		return TOKEN_NORMAL;

	if (!strncasecmp(token, "http://", 7)) {
		if (is_valid_domain(token + 7))
			return TOKEN_HTTP;
		else
			return TOKEN_NORMAL;
	}

	if (!strncasecmp(token, "ftp://", 6))
		return TOKEN_FTP;
	if (!strncasecmp(token, "log://", 6))
		return TOKEN_CUSTOM;
	if (!strncasecmp(token, "mailto:", 7))
		return TOKEN_EMAIL;
	if (!strncasecmp(token, "www.", 4) && is_valid_domain(token))
		return TOKEN_HTTP;
	if (!strncasecmp(token, "ftp.", 4) && is_valid_domain(token))
		return TOKEN_FTP;
	if (strstr(token, "://") && !ispunct(token[0])
		&& !ispunct(token[strlen(token)]))
		return TOKEN_CUSTOM;

	for (i = 0; i < len; i++) {
		if (token[i] == '@') {
			if (!ispunct(token[0]) && !ispunct(token[len - 1])) {
				if (is_valid_domain(token + i + 1))
					return TOKEN_EMAIL;
				break;
			}
		}
	}

	for (i = len; i >= 0; i--) {
		if (token[i] == '.') {
			if (!strcasecmp(token + i, ".edu")
				&& is_valid_domain(token))
				return TOKEN_HTTP;
			if (!strcasecmp(token + i, ".com")
				&& is_valid_domain(token))
				return TOKEN_HTTP;
			if (!strcasecmp(token + i, ".net")
				&& is_valid_domain(token))
				return TOKEN_HTTP;
			if (!strcasecmp(token + i, ".org")
				&& is_valid_domain(token))
				return TOKEN_HTTP;
			if (!strcasecmp(token + i, ".gov")
				&& is_valid_domain(token))
				return TOKEN_HTTP;
			break;
		}
	}
	return TOKEN_NORMAL;
}

static GString *get_next_token(const char *input)
{
	int i = 0;
	int len = strlen(input);
	GString *string = g_string_sized_new(20);

	if (!strncasecmp(input, "<A", 2)) {
		g_string_assign(string, "<A");
		for (i = 2; i < len; i++) {
			g_string_append_c(string, input[i]);
			if (input[i] != '<')
				continue;
			g_string_append_c(string, input[++i]);
			if (input[i] != '/')
				continue;
			g_string_append_c(string, input[++i]);
			if (input[i] != 'a' && input[i] != 'A')
				continue;
			g_string_append_c(string, input[++i]);
			break;
		}
		return string;
	}
	if (input[0] == '<') {
		for (i = 0; i < len; i++) {
			g_string_append_c(string, input[i]);
			if (input[i] == '>')
				break;
		}
		return string;
	}

	if (ispunct(input[0])) {
		for (i = 0; i < len; i++) {
			if (ispunct(input[i]) && input[i] != '<')
				g_string_append_c(string, input[i]);
			else
				break;
		}
		return string;
	}

	/*
	 * now that we have covered prior html
	 * we can do an (almost) simple word tokenization
	 */

	for (i = 0; i < len; i++) {
		if (!isspace(input[i]) && input[i] != '<') {
			if (!ispunct(input[i]) || input[i] == '/')
				g_string_append_c(string, input[i]);
			else {
				int j;
				for (j = i + 1; j < len; j++) {
					if (isspace(input[j]))
						return string;
					if (isalpha(input[j])
						|| isdigit(input[j]))
						break;
				}
				if (j == len)
					return string;
				else
					g_string_append_c(string, input[i]);
			}
		} else
			return string;
	}
	return string;
}

static void linkify_token(GString *token)
{
	GString *g;
	GString *g2;
	int type = is_link(token->str);

	if (type == TOKEN_NORMAL)
		return;

	g = g_string_sized_new(token->len);
	g2 = g_string_sized_new(token->len);

	g_string_assign(g, token->str);
	g_string_assign(g2, token->str);

	if (type == TOKEN_HTTP && strncasecmp(token->str, "http://", 7))
		g_string_prepend(g2, "http://");
	else if (type == TOKEN_FTP && strncasecmp(token->str, "ftp://", 6))
		g_string_prepend(g2, "ftp://");
	else if (type == TOKEN_EMAIL && strncasecmp(token->str, "mailto:", 7))
		g_string_prepend(g2, "mailto:");

	eb_debug(DBG_HTML, "TOKEN: %s\n", token->str);
	/* g_string_sprintf is safe */
	g_string_sprintf(token, "<A HREF=\"%s\">%s</A>", g2->str, g->str);

	g_string_free(g, TRUE);
	g_string_free(g2, TRUE);
}

char *linkify(const char *input)
{
	int i = 0;
	int len = strlen(input);
	char *result;
	GString *temp_result;
	GString *temp = NULL;

	temp_result = g_string_sized_new(2048);

	while (i < len) {
		if (isspace(input[i])) {
			g_string_append_c(temp_result, input[i]);
			i++;
		} else {
			temp = get_next_token(input + i);
			eb_debug(DBG_HTML, "%s\t%s\t%d\t%d\n", input, input + i,
				i, temp->len);
			i += temp->len;
			linkify_token(temp);
			g_string_append(temp_result, temp->str);
			g_string_free(temp, TRUE);
		}
	}

	result = temp_result->str;
	g_string_free(temp_result, FALSE);
	return result;
}

char *ay_linkify_filter(Conversation *conv, const char *s)
{
	return linkify(s);
}

char *ay_smilify_filter(Conversation *conv, const char *s)
{
	/* TODO make this a filter */
	if (iGetLocalPref("do_smiley")
		&& RUN_SERVICE(conv->local_user)->get_smileys) {
		return eb_smilify(s,
			RUN_SERVICE(conv->local_user)->get_smileys(),
			get_service_name(conv->local_user->service_id));
	}

	return strdup(s);
}

char *ay_convert_eol_filter(Conversation *conv, char *text)
{
	char *temp;
	char **data = NULL;
	int i;

	if (!text || !*text)
		return g_strdup("");

	if (strstr(text, "\r\n") != NULL)
		return g_strdup(text);

	data = g_strsplit(text, "\n", 64);
	temp = g_strdup(data[0]);
	for (i = 1; data[i] != NULL; i++) {
		temp = g_strdup_printf("%s\r\n%s", temp, data[i]);
	}
	g_strfreev(data);
	return temp;
}

static grouplist *dummy_group = NULL;

/* Parameters:
	remote - the account to find a local account for
	online - whether the local account needs to be online
*/
eb_local_account *find_local_account_for_remote(eb_account *remote, int online)
{
	static LList *node = NULL;
	static eb_account *last_remote = NULL;

	/* If this is a normal call, start at the top and give the first, otherwise continue where we left off */
	if (remote) {
		node = accounts;
		last_remote = remote;
		if (remote->ela && (!online || remote->ela->connected))
			return remote->ela;
	} else {
		remote = last_remote;
		if (node)
			node = node->next;
	}

	for (; node; node = node->next) {
		eb_local_account *ela = node->data;

		if (remote->service_id == ela->service_id) {
			if ((!online || ela->connected) &&
				(!RUN_SERVICE(ela)->is_suitable ||
					RUN_SERVICE(ela)->is_suitable(ela,
						remote)))
				return ela;
		}
	}

	/*We can't find anything, let's bail */
	return NULL;
}

int connected_local_accounts(void)
{
	int n = 0;
	LList *node = NULL;

	for (node = accounts; node; node = node->next) {
		eb_local_account *ela = node->data;
		if (ela->connected || ela->connecting) {
			n++;
			eb_debug(DBG_CORE, "%s: connected=%d, connecting=%d\n",
				ela->handle, ela->connected, ela->connecting);
		}
	}
	return n;
}

eb_local_account *find_suitable_local_account(eb_local_account *first,
	int second)
{
	LList *node;

	if (first && (first->connected || first->connecting))
		return first;

	/*dang, we are out of luck with our first choice, do we have something
	   else that uses the same service? */

	for (node = accounts; node; node = node->next) {
		eb_local_account *ela = node->data;
		eb_debug(DBG_CORE, "%s %s\n", get_service_name(ela->service_id),
			ela->handle);

		if (ela->service_id == second && (ela->connected
				|| ela->connecting))
			return ela;
		else if (!ela->connected)
			eb_debug(DBG_CORE, "%s is offline!\n", ela->handle);

	}

	/*We can't find anything, let's bail */
	return NULL;
}

/**
 * Find a local account that can currently send messages to a given remote account
 * @ea	- the remote account to send messages to
 * @ela - a preferred local account if any, can be NULL
 *
 * @return a local account that can send messages to the given remote account, first
 * priority given to preferred account, second priority given to attached account,
 * last priority given to an account with the same service id
 */
eb_local_account *find_suitable_local_account_for_remote(eb_account *ea,
	eb_local_account *ela)
{
	if (ela && (ela->connected || ela->connecting))
		return ela;

	if (ea->ela && (ea->ela->connected || ea->ela->connecting))
		return ea->ela;

	return find_suitable_local_account(NULL, ea->service_id);
}

/* if contact can offline message, return the account, otherwise, return null */

eb_account *can_offline_message(struct contact *con)
{
	LList *node;
	for (node = con->accounts; node; node = node->next) {
		eb_account *ea = node->data;

		if (can_offline_msg(eb_services[ea->service_id]))
			return ea;
	}
	return NULL;
}

eb_account *find_suitable_remote_account(eb_account *first,
	struct contact *rest)
{
	LList *node;
	eb_account *possibility = NULL;

	if (first && RUN_SERVICE(first)->query_connected(first)
		&& first->ela->connected)
		return first;

	for (node = rest->accounts; node; node = node->next) {
		eb_account *ea = node->data;

		if (RUN_SERVICE(ea)->query_connected(ea) && ea->ela
			&& ea->ela->connected) {
			if (ea->service_id == rest->default_chatb)
				return ea;
			else
				possibility = ea;
		}
	}
	if (possibility)
		return possibility;

	if (first && first->ela && first->ela->connected
		&& (can_offline_msg(eb_services[first->service_id])
			|| RUN_SERVICE(first)->query_connected(first)))
		return first;

	return NULL;
}

eb_account *find_suitable_file_transfer_account(eb_account *first,
	struct contact *rest)
{
	LList *node;
	eb_account *possibility = NULL;

	if (first == NULL)
		return NULL;

	if (first && RUN_SERVICE(first)->query_connected(first)
		&& can_file_transfer(eb_services[first->service_id]))
		return first;

	for (node = rest->accounts; node; node = node->next) {
		eb_account *ea = node->data;

		if (RUN_SERVICE(ea)->query_connected(ea)
			&& can_file_transfer(eb_services[first->service_id])) {
			if (ea->service_id == rest->default_chatb)
				return ea;
			else
				possibility = ea;
		}
	}
	return possibility;
}

grouplist *find_grouplist_by_name(const char *name)
{
	LList *l1;

	if (name == NULL)
		return NULL;

	for (l1 = groups; l1; l1 = l1->next) {
		if (!strcasecmp(((grouplist *)l1->data)->name, name))
			return l1->data;
	}
	return NULL;
}

grouplist *find_grouplist_by_nick(char *nick)
{
	LList *l1, *l2;

	if (nick == NULL)
		return NULL;

	for (l1 = groups; l1; l1 = l1->next)
		for (l2 = ((grouplist *)l1->data)->members; l2; l2 = l2->next)
			if (!strcmp(((struct contact *)l2->data)->nick, nick))
				return l1->data;

	return NULL;

}

struct contact *find_contact_by_handle(const char *handle)
{
	LList *l1, *l2, *l3;

	if (handle == NULL)
		return NULL;

	for (l1 = groups; l1; l1 = l1->next) {
		for (l2 = ((grouplist *)l1->data)->members; l2; l2 = l2->next) {
			for (l3 = ((struct contact *)l2->data)->accounts; l3;
				l3 = l3->next) {
				eb_account *account = (eb_account *)l3->data;
				if (!strcmp(account->handle, handle))
					return (struct contact *)l2->data;
			}
		}
	}
	return NULL;
}

struct contact *find_contact_by_nick(const char *nick)
{
	LList *l1, *l2;

	if (nick == NULL)
		return NULL;

	for (l1 = groups; l1; l1 = l1->next) {
		for (l2 = ((grouplist *)l1->data)->members; l2; l2 = l2->next) {
			if (!strcasecmp(((struct contact *)l2->data)->nick,
					nick))
				return (struct contact *)l2->data;
		}
	}
	return NULL;
}

struct contact *find_contact_in_group_by_nick(const char *nick, grouplist *gl)
{
	LList *l;

	if (nick == NULL || gl == NULL)
		return NULL;

	for (l = gl->members; l; l = l->next) {
		if (!strcasecmp(((struct contact *)l->data)->nick, nick))
			return (struct contact *)l->data;
	}

	return NULL;
}

static eb_account *find_account_with_id_or_ela(const char *handle,
	int service_id, eb_local_account *ela)
{
	LList *l1, *l2, *l3;

	if (handle == NULL)
		return NULL;

/* go on as long as there's groups in groups or if we have a dummy_group */
	for (l1 = groups; l1 || dummy_group; l1 = l1->next) {
		grouplist *g = NULL;
		/* if groups is over, then check the dummy_group */
		if (l1)
			g = l1->data;
		else
			g = dummy_group;

		for (l2 = g->members; l2; l2 = l2->next) {
			for (l3 = ((struct contact *)l2->data)->accounts; l3;
				l3 = l3->next) {
				eb_account *ea = l3->data;

				if (((ela && ea->ela == ela) || (!ela
							&& ea->service_id ==
							service_id))
					&& !strcasecmp(handle, ea->handle))
					return ea;
			}
		}

		/* if groups is over, then we've also finished with dummy */
		if (!l1)
			break;
	}
	return NULL;
}

eb_account *find_account_by_handle(const char *handle, int service_id)
{
	LList *w = accounts;
	eb_account *found = NULL;
	for (; w; w = w->next) {
		eb_local_account *ela = (eb_local_account *)w->data;
		if (ela->service_id == service_id)
			found = find_account_with_ela(handle, ela);
		if (found) {
			eb_debug(DBG_CORE, "%s: found ea with ela: %s\n",
				handle, ela->handle);
			return found;
		}
	}
	/* if the right ela isn't found, return possible ea with no ela */
	found = find_account_with_id_or_ela(handle, service_id, NULL);
	if (found)
		eb_debug(DBG_CORE,
			"%s: found ea with ela (should be null): %p\n", handle,
			found->ela);
	return found;
}

eb_account *find_account_with_ela(const char *handle, eb_local_account *ela)
{
	if (!ela)
		return NULL;
	return find_account_with_id_or_ela(handle, ela->service_id, ela);
}

eb_local_account *find_local_account_by_handle(const char *handle, int type)
{
	LList *l1;

	if (handle == NULL)
		return NULL;

	for (l1 = accounts; l1; l1 = l1->next) {
		eb_local_account *account = (eb_local_account *)l1->data;
		if (account->service_id == type
			&& !strcasecmp(account->handle, handle))
			return account;
	}
	return NULL;
}

void strip_html(char *text)
{
	int i, j;
	int visible = 1;

	for (i = 0, j = 0; text[i]; i++) {
		if (text[i] == '<') {
			switch (text[i + 1]) {
			case 'a':
			case 'A':
				if (isspace(text[i + 2]) || text[i + 2] == '>')
					visible = 0;
				break;

			case 'i':
			case 'I':
				if (!strncasecmp(text + i + 1, "img ", 4))
					visible = 0;	/* don't break */
			case 'u':
			case 'U':
				if (text[i + 2] == '>')
					visible = 0;
				break;
			case 'p':
			case 'P':
				if (text[i + 2] == '>'
					|| !strncasecmp(text + i + 1, "p    ",
						5)
					|| !strncasecmp(text + i + 1, "p   >",
						5))
					visible = 0;

				break;
			case 'b':
			case 'B':
				if (text[i + 2] == '>'
					|| !strncasecmp(text + i + 1, "br>", 3)
					|| !strncasecmp(text + i + 1, "body ",
						5)
					|| !strncasecmp(text + i + 1, "body>",
						5))
					visible = 0;
				break;
			case 'h':
			case 'H':
				if (!strncasecmp(text + i + 1, "html ", 5)
					|| !strncasecmp(text + i + 1, "hr>", 3)
					|| !strncasecmp(text + i + 1,
						"hr width", 8)
					|| !strncasecmp(text + i + 1, "html>",
						5))
					visible = 0;
				break;
			case 'F':
			case 'f':
				if (!strncasecmp(text + i + 1, "font ", 5)
					|| !strncasecmp(text + i + 1, "font>",
						5))
					visible = 0;
				break;
			case 's':
				if (!strncasecmp(text + i + 2, "miley", 5)) {
					visible = 0;
					text[j++] = ' ';	/*hack */
				}
			case '!':
				if (!strncasecmp(text + i + 1, "!--", 3))
					visible = 0;
				break;
			case '/':
				visible = 0;
				break;
			}
		} else if (text[i] == '>') {
			if (!visible)
				visible = 1;
			continue;
		}
		if (visible)
			text[j++] = text[i];
	}
	text[j] = '\0';
}

int remove_account(eb_account *ea)
{
	struct contact *c = ea->account_contact;

	if (!find_suitable_local_account_for_remote(ea, NULL))
		contact_mgmt_queue_add(ea, MGMT_DEL, NULL);

	buddy_logoff(ea);
	remove_account_line(ea);
	c->accounts = l_list_remove(c->accounts, ea);

	RUN_SERVICE(ea)->del_user(ea);
	g_free(ea);

	if (l_list_empty(c->accounts)) {
		remove_contact(c);
		return 0;	/* so if coming from remove_contact 
				   don't try again to remove_contact_line() 
				   and so on */
	}

	return 1;
}

void remove_contact(struct contact *c)
{
	grouplist *g = c->group;

	if (c->conversation)
		ay_conversation_end(c->conversation);

	while (c->accounts && !l_list_empty(c->accounts))
		if (!remove_account(c->accounts->data))
			return;

	remove_contact_line(c);
	g->members = l_list_remove(g->members, c);
	g_free(c);
}

void remove_group(grouplist *g)
{
	LList *node = NULL;
	while (g->members)
		remove_contact(g->members->data);

	remove_group_line(g);
	groups = l_list_remove(groups, g);

	for (node = accounts; node; node = node->next) {
		eb_local_account *ela = (eb_local_account *)(node->data);
		if (ela->connected && RUN_SERVICE(ela)->del_group) {
			eb_debug(DBG_CORE, "dropping group %s in %s\n",
				g->name, get_service_name(ela->service_id));
			RUN_SERVICE(ela)->del_group(ela, g->name);
		} else if (RUN_SERVICE(ela)->del_group) {
			group_mgmt_queue_add(ela, g->name, MGMT_GRP_DEL, NULL);
		}

	}
	g_free(g);
}

void add_group(const char *name)
{
	LList *node = NULL;
	grouplist *eg;

	eg = calloc(1, sizeof(grouplist));

	strncpy(eg->name, name, sizeof(eg->name));

	groups = l_list_prepend(groups, eg);
	add_group_line(eg);

	for (node = accounts; node; node = node->next) {
		eb_local_account *ela = node->data;
		if (RUN_SERVICE(ela)->add_group) {
			if (ela->connected) {
				eb_debug(DBG_CORE, "adding group %s in %s\n",
					name,
					get_service_name(ela->service_id));
				RUN_SERVICE(ela)->add_group(ela, name);
			} else {
				group_mgmt_queue_add(ela, NULL, MGMT_GRP_ADD,
					name);
			}
		}
	}
	update_contact_list();
	write_contact_list();
}

void rename_group(grouplist *current_group, const char *new_name)
{
	LList *node = NULL;
	char oldname[NAME_MAX];

	strncpy(oldname, current_group->name, sizeof(oldname));
	strncpy(current_group->name, new_name, sizeof(current_group->name));
	update_group_line(current_group);
	update_contact_list();
	write_contact_list();

	for (node = current_group->members; node; node = node->next) {
		struct contact *c = (struct contact *)node->data;
		if (c)
			rename_nick_log(oldname, c->nick, new_name, c->nick);
	}

	for (node = accounts; node; node = node->next) {
		eb_local_account *ela = node->data;
		if (RUN_SERVICE(ela)->rename_group) {
			if (ela->connected) {
				eb_debug(DBG_CORE,
					"renaming group %s to %s in %s\n",
					oldname, new_name,
					get_service_name(ela->service_id));
				RUN_SERVICE(ela)->rename_group(ela, oldname,
					new_name);
			} else {
				group_mgmt_queue_add(ela, oldname, MGMT_GRP_REN,
					new_name);
			}
		}
	}
}

int account_cmp(const void *a, const void *b)
{
	eb_account *ca = (eb_account *)a, *cb = (eb_account *)b;
	int cmp = 0;
	cmp = ca->priority - cb->priority;
	if (cmp == 0)
		cmp = strcasecmp(ca->handle, cb->handle);
	if (cmp == 0)
		cmp = strcasecmp(get_service_name(ca->service_id),
			get_service_name(cb->service_id));
	if (cmp == 0)
		cmp = strcasecmp(ca->ela->handle, cb->ela->handle);

	return cmp;
}

/* compares two contact names */
int contact_cmp(const void *a, const void *b)
{
	const struct contact *ca = a, *cb = b;
	return strcasecmp(ca->nick, cb->nick);
}

int group_cmp(const void *a, const void *b)
{
	const grouplist *ga = a, *gb = b;
	return strcasecmp(ga->name, gb->name);
}

struct relocate_account_data {
	eb_account *account;
	struct contact *contact;
};

static void move_account_yn(void *data, int result)
{
	struct relocate_account_data *rad = data;

	if (result)
		move_account(rad->contact, rad->account);
	else if (l_list_empty(rad->contact->accounts))
		remove_contact(rad->contact);

	free(rad);
}

static void add_account_verbose(char *contact, eb_account *ea, int verbosity)
{
	struct relocate_account_data *rad;
	struct contact *c = find_contact_by_nick(contact);
	eb_account *account;
	if (ea->ela)
		account = find_account_with_ela(ea->handle, ea->ela);
	else
		account = find_account_by_handle(ea->handle, ea->service_id);

	if (account) {
		if (!strcasecmp(account->account_contact->group->name,
				_("Unknown"))
			|| strstr(account->account_contact->group->name,
				"__Ayttm_Dummy_Group__")
			== account->account_contact->group->name) {
			move_account(c, account);
		} else {
			char buff[2048];
			snprintf(buff, sizeof(buff),
				_("The account \"%s\" already exists on your\n"
					"contact list at the following location\n"
					"\tGroup: %s\n"
					"\tContact: %s\n"
					"\nShould I move it?"),
				account->handle,
				account->account_contact->group->name,
				account->account_contact->nick);

			/* remove this contact if it was newly created */
			if (verbosity) {
				rad = calloc(1,
					sizeof(struct relocate_account_data));
				rad->account = account;
				rad->contact = c;
				eb_do_dialog(buff, _("Error: account exists"),
					move_account_yn, rad);
			} else if (c && l_list_empty(c->accounts))
				remove_contact(c);
		}
		return;
	}
	if (!find_suitable_local_account_for_remote(ea, NULL)) {
		contact_mgmt_queue_add(ea, MGMT_ADD,
			c ? c->group->name : _("Unknown"));
	}
	if (c) {
		c->accounts =
			l_list_insert_sorted(c->accounts, ea, account_cmp);
		ea->account_contact = c;
		RUN_SERVICE(ea)->add_user(ea);

		if (!strcmp(c->group->name, _("Ignore")) &&
			RUN_SERVICE(ea)->ignore_user)
			RUN_SERVICE(ea)->ignore_user(ea);
	} else
		add_unknown_with_name(ea, contact);

	update_contact_list();
	write_contact_list();
}

void add_account_silent(char *contact, eb_account *ea)
{
	add_account_verbose(contact, ea, FALSE);
}

void add_account(char *contact, eb_account *ea)
{
	add_account_verbose(contact, ea, TRUE);
}

static void add_contact(const char *group, struct contact *user)
{
	grouplist *grp = find_grouplist_by_name(group);

	if (!grp) {
		add_group(group);
		grp = find_grouplist_by_name(group);
	}
	if (!grp) {
		eb_debug(DBG_CORE, "Error adding group :(\n");
		return;
	}
	grp->members = l_list_insert_sorted(grp->members, user, contact_cmp);
	user->group = grp;
}

static struct contact *create_contact(const char *con, int type)
{
	struct contact *c = calloc(1, sizeof(struct contact));
	if (con != NULL)
		strncpy(c->nick, con, sizeof(c->nick));

	c->default_chatb = c->default_filetransb = type;

	return (c);
}

struct contact *add_new_contact(const char *group, const char *con, int type)
{
	struct contact *c = create_contact(con, type);

	add_contact(group, c);

	return c;
}

/* used to chat with someone without adding him to your buddy list */
struct contact *add_dummy_contact(const char *con, eb_account *ea)
{
	struct contact *c = create_contact(con, ea->service_id);

	c->accounts = l_list_prepend(c->accounts, ea);
	ea->account_contact = c;
	ea->icon_handler = ea->status_handler = -1;

	if (!dummy_group) {
		dummy_group = calloc(1, sizeof(grouplist));
		/* don't translate this string */
		snprintf(dummy_group->name, sizeof(dummy_group->name),
			"__Ayttm_Dummy_Group__%d__", rand());
	}

	dummy_group->members = l_list_prepend(dummy_group->members, c);
	c->group = dummy_group;
	c->online = 1;
	return c;
}

void clean_up_dummies()
{
	LList *l, *l2;

	if (!dummy_group)
		return;

	for (l = dummy_group->members; l; l = l->next) {
		struct contact *c = l->data;
		for (l2 = c->accounts; l2; l2 = l2->next) {
			eb_account *ea = l2->data;
			if (CAN(ea, free_account_data))
				RUN_SERVICE(ea)->free_account_data(ea);
			free(ea);
		}
		l_list_free(c->accounts);
		free(c);
	}
	l_list_free(dummy_group->members);
	free(dummy_group);
}

void add_unknown_with_name(eb_account *ea, char *name)
{
	struct contact *con = add_new_contact(_("Unknown"),
		(name && strlen(name)) ? name : ea->handle, ea->service_id);

	con->accounts = l_list_prepend(con->accounts, ea);
	ea->account_contact = con;
	ea->icon_handler = ea->status_handler = -1;
	if (find_suitable_local_account_for_remote(ea, NULL))
		RUN_SERVICE(ea)->add_user(ea);
	else {
		contact_mgmt_queue_add(ea, MGMT_ADD,
			ea->account_contact->group->name);
	}
	write_contact_list();
}

void add_unknown(eb_account *ea)
{
	add_unknown_with_name(ea, NULL);
}

static void handle_group_change(eb_account *ea, char *og, const char *ng)
{
	/* if the groups are same, do nothing */
	if (!strcasecmp(ng, og))
		return;

	if (!find_suitable_local_account_for_remote(ea, NULL)) {
		contact_mgmt_queue_add(ea, MGMT_MOV, ng);
	}

	/* adding to ignore */
	if (!strcmp(ng, _("Ignore")) && RUN_SERVICE(ea)->ignore_user)
		RUN_SERVICE(ea)->ignore_user(ea);

	/* remove from ignore */
	else if (!strcmp(og, _("Ignore")) && RUN_SERVICE(ea)->unignore_user)
		RUN_SERVICE(ea)->unignore_user(ea, ng);

	/* just your regular group change */
	else if (RUN_SERVICE(ea)->change_group)
		RUN_SERVICE(ea)->change_group(ea, ng);

}

/**
 * Move an account from one contact to another
 * @new_con - the contact to move the account to
 * @ea      - the account to move
 *
 * @return  - the old contact
 */
struct contact *move_account(struct contact *new_con, eb_account *ea)
{
	struct contact *old_con = ea->account_contact;
	char *new_group = new_con->group->name;
	char *old_group = NULL;

	if (old_con != new_con && old_con && old_con->group) {
		old_group = old_con->group->name;

		if (old_group)
			handle_group_change(ea, old_group, new_group);

		if (old_con)
			old_con->accounts =
				l_list_remove(old_con->accounts, ea);
		remove_account_line(ea);

		new_con->accounts =
			l_list_insert_sorted(new_con->accounts, ea,
			account_cmp);
		ea->account_contact = new_con;

		if (old_con && l_list_empty(old_con->accounts)) {
			remove_contact(old_con);
			old_con = NULL;
		} else if (old_con) {
			LList *l;
			old_con->online = 0;
			for (l = old_con->accounts; l; l = l->next)
				if (((eb_account *)l->data)->online)
					old_con->online++;
			if (!old_con->online)
				remove_contact_line(old_con);
			else
				add_contact_and_accounts(old_con);
		}

		if (ea->online) {
			new_con->online++;
			add_contact_line(new_con);
		}
	}

	add_contact_and_accounts(new_con);

	write_contact_list();

	return old_con;
}

void move_contact(const char *group, struct contact *c)
{
	grouplist *g = c->group;
	struct contact *con;
	LList *l = c->accounts;

	if (!strcasecmp(g->name, group))
		return;

	g->members = l_list_remove(g->members, c);
	remove_contact_line(c);
	g = find_grouplist_by_name(group);

	if (!g) {
		add_group(group);
		g = find_grouplist_by_name(group);
	}
	add_group_line(g);

	rename_nick_log(c->group->name, c->nick, group, c->nick);

	for (; l; l = l->next) {
		eb_account *ea = l->data;
		handle_group_change(ea, c->group->name, group);
	}

	con = find_contact_in_group_by_nick(c->nick, g);
	if (con && con == c) {
		/* that's the same */
		return;
	} else if (con) {
		/* contact already exists in new group : 
		   move accounts for old contact to new one */
		while (c && (l = c->accounts)) {
			eb_account *ea = l->data;
			c = move_account(con, ea);
		}
		return;
	} else if (!con) {
		g->members = l_list_insert_sorted(g->members, c, contact_cmp);
		c->group = g;
		add_contact_and_accounts(c);
		add_contact_line(c);
	}
}

void rename_contact(struct contact *c, const char *newname)
{
	LList *l = NULL;
	struct contact *con = NULL;

	if (!c || !strcmp(c->nick, newname))
		return;

	eb_debug(DBG_CORE, "Renaming %s to %s\n", c->nick, newname);

	/* name differs in case only */
	if (strcasecmp(c->nick, newname))
		con = find_contact_in_group_by_nick(newname, c->group);
	if (con) {
		eb_debug(DBG_CORE, "found existing contact\n");
		rename_nick_log(c->group->name, c->nick, c->group->name,
			con->nick);
		while (c && (l = c->accounts)) {
			eb_account *ea = l->data;
			c = move_account(con, ea);
		}
		update_contact_list();
		write_contact_list();

	} else {
		eb_debug(DBG_CORE,
			"no existing contact, or case change only\n");

		rename_nick_log(c->group->name, c->nick, c->group->name,
			newname);
		strncpy(c->nick, newname, sizeof(c->nick) - 1);
		c->nick[sizeof(c->nick) - 1] = '\0';

		c->label = strdup(newname);
		update_contact_line(c);

		l = c->accounts;
		while (l) {
			eb_account *ea = l->data;

			if (RUN_SERVICE(ea)->change_user_name) {
				if (find_suitable_local_account_for_remote(ea,
						NULL))
					contact_mgmt_queue_add(ea, MGMT_REN,
						newname);
				else
					RUN_SERVICE(ea)->change_user_name(ea,
						newname);
			}

			l = l->next;
		}
	}
	update_contact_list();
	write_contact_list();
}

typedef struct _invite_request {
	eb_local_account *ela;
	void *id;
} invite_request;

static void process_invite(void *data, int result)
{
	invite_request *invite = data;

	if (result && RUN_SERVICE(invite->ela)->accept_invite)
		RUN_SERVICE(invite->ela)->accept_invite(invite->ela,
			invite->id);
	else if (RUN_SERVICE(invite->ela)->decline_invite)
		RUN_SERVICE(invite->ela)->decline_invite(invite->ela,
			invite->id);

	g_free(invite);
}

void invite_dialog(eb_local_account *ela, const char *user,
		   const char *chat_room, void *id)
{
	char *message =
		g_strdup_printf(_
		("User %s wants to invite you to\n%s\nWould you like to accept?"),
		user, chat_room);
	invite_request *invite = g_new0(invite_request, 1);

	invite->ela = ela;
	invite->id = id;
	eb_do_dialog(message, _("Chat Invite"), process_invite, invite);
	g_free(message);
}

void make_safe_filename(char *buff, const char *name, const char *group)
{

	/* i'm pretty sure the only illegal character is '/', but maybe 
	 * there are others i'm forgetting */
	char *bad_chars = "/";
	char *p;
	char holder[NAME_MAX], gholder[NAME_MAX];

	strncpy(holder, name, NAME_MAX);
	if (group != NULL)
		strncpy(gholder, group, NAME_MAX);

	for (p = holder; *p; p++) {
		if (strchr(bad_chars, *p))
			*p = '_';
	}
	if (group != NULL)
		for (p = gholder; *p; p++) {
			if (strchr(bad_chars, *p))
				*p = '_';
		}

	g_snprintf(buff, NAME_MAX, "%slogs/%s%s%s",
		config_dir,
		(group != NULL ? gholder : ""),
		(group != NULL ? "-" : ""), holder);
	eb_debug(DBG_CORE, "logfile: %s\n", buff);
}

/* looks for lockfile, if found, returns the pid in the file */
/* If not, creates the file, writes our pid, and returns -1 */
pid_t create_lock_file(char *fname)
{
	pid_t ourpid = -1;
#ifndef __MINGW32__
	struct stat sbuff;
	FILE *f;

	if (stat(fname, &sbuff) != 0) {
		/* file doesn't exist, so we're gonna open it to write out pid to it */
		ourpid = getpid();
		if ((f = fopen(fname, "a")) != NULL) {
			fprintf(f, "%d\n", ourpid);
			fclose(f);
			ourpid = -1;
		} else {
			ourpid = 0;	/* I guess could be considered an error condition */
			/* in that we couldn't create the lock file */
		}
	} else {
		/* this means that the file exists */
		if ((f = fopen(fname, "r")) != NULL) {
			char data[64], data2[64];
			fscanf(f, "%d", &ourpid);
			fclose(f);
			snprintf(data, sizeof(data), "/proc/%d", ourpid);
			if (stat(data, &sbuff) != 0) {
				fprintf(stderr,
					_("deleting stale lock file\n"));
				unlink(fname);	/*delete lock file and try again :) */
				return create_lock_file(fname);
			} else {
				FILE *fd = NULL;
				snprintf(data2, sizeof(data2), "%s/cmdline",
					data);
				fd = fopen(data2, "r");
				if (fd == NULL) {
					perror("fopen");
				} else {
					char cmd[1024];
					fgets(cmd, sizeof(cmd), fd);
					printf("registered PID [%d] is from %s\n", ourpid, cmd);
					fclose(fd);
					if (cmd == NULL
						|| strstr(cmd,
							"ayttm") == NULL) {
						fprintf(stderr,
							_
							("deleting stale lock file\n"));
						unlink(fname);	/*delete lock file and try again :) */
						return create_lock_file(fname);
					}
				}
			}
		} else {
			/* couldn't open it... bizarre... allow the program to run anyway... heh */
			ourpid = -1;
		}
	}
#endif
	return ourpid;
}

void delete_lock_file(char *fname)
{
#ifndef __MINGW32__
	unlink(fname);
#endif
}

void eb_generic_menu_function(void *add_button, void *userdata)
{
	menu_item_data *mid = userdata;
	ebmCallbackData *ecd = NULL;

	assert(userdata);
	ecd = mid->data;
	/* Check for valid data type */
	if (!ecd || !IS_ebmCallbackData(ecd)) {
		g_warning(_
			("Unexpected datatype passed to eb_generic_menu_function, ignoring call!"));
		return;
	}
	if (!mid->callback) {
		g_warning(_
			("No callback defined in call to eb_generic_menu_function, ignoring call!"));
		return;
	}
	eb_debug(DBG_CORE, "Calling callback\n");
	mid->callback(ecd);
}

/*
 * This function just gets a linked list of groups, this is handy
 * because we can then use it to populate the groups combo widget
 * with this information
 */

LList *get_groups()
{
	LList *node = NULL;
	LList *newlist = NULL;

	for (node = groups; node; node = l_list_next(node))
		newlist =
			l_list_insert_sorted(newlist,
			((grouplist *)node->data)->name,
#ifdef _AIX
			(LListCompFunc) g_strcasecmp
#else
			(LListCompFunc) strcasecmp
#endif
			);

	return newlist;
}

void rename_nick_log(char *oldgroup, char *oldnick, const char *newgroup,
	const char *newnick)
{
	/* bug 929347 */
	LList *utility_walk;
	char oldnicklog[255], newnicklog[255], buff[NAME_MAX];
	FILE *test = NULL;
	make_safe_filename(buff, oldnick, oldgroup);
	strncpy(oldnicklog, buff, 255);

	make_safe_filename(buff, newnick, newgroup);
	strncpy(newnicklog, buff, 255);

#ifdef _WIN32
	/* Win32 file structure is case insensitive */
	if (!strcasecmp(oldnicklog, newnicklog))
#else
	if (!strcmp(oldnicklog, newnicklog))
#endif
		return;

	if ((test = fopen(newnicklog, "r")) != NULL
		&& strcmp(oldnicklog, newnicklog)) {
		FILE *oldfile = fopen(oldnicklog, "r");
		char read_buffer[4096];

		fclose(test);
		test = fopen(newnicklog, "a");

		if (oldfile) {
			while (fgets(read_buffer, 4096, oldfile) != NULL)
				fputs(read_buffer, test);

			fclose(oldfile);
		}
		fclose(test);
		unlink(oldnicklog);
		eb_debug(DBG_CORE, "Copied log from %s to %s\n", oldnicklog,
			newnicklog);
	}

	else if (strcmp(oldnicklog, newnicklog)) {
		rename(oldnicklog, newnicklog);
		eb_debug(DBG_CORE, "Renamed log from %s to %s\n", oldnicklog,
			newnicklog);
	}
	/* bug 929347 */
	for (utility_walk = nick_modify_utility; utility_walk;
		utility_walk = utility_walk->next) {
		void (*ifilter) (char *onick, const char *nnick);

		ifilter = utility_walk->data;
		ifilter(oldnick, newnick);
	}
}

eb_account *find_account_for_protocol(struct contact *c, int service)
{
	LList *l = c->accounts;
	while (l && l->data) {
		eb_account *a = (eb_account *)l->data;
		if (a && a->service_id == service)
			return a;
		l = l->next;
	}
	return NULL;
}

GList *llist_to_glist(LList *ll, int free_old)
{
	GList *g = NULL;
	LList *l = ll;

	for (; l; l = l->next)
		g = g_list_append(g, l->data);

	if (free_old)
		l_list_free(ll);

	return g;
}

LList *glist_to_llist(GList *gl, int free_old)
{
	LList *l = NULL;
	GList *g = gl;

	for (; g; g = g->next)
		l = l_list_append(l, g->data);

	if (free_old)
		g_list_free(gl);

	return l;
}

int send_url(const char *url)
{
	LList *node;
	int ret = 0;
	for (node = accounts; node && !ret; node = node->next) {
		eb_local_account *ela = node->data;
		if (CAN(ela, handle_url)) {
			ret = RUN_SERVICE(ela)->handle_url(url);
			eb_debug(DBG_CORE, "%s: %s handle this url\n",
				ela->handle, ret ? "did" : "didn't");
		} else {
			eb_debug(DBG_CORE, "%s: no handle of this url\n",
				ela->handle);
		}

	}
	return ret;
}

int eb_send_message(const char *to, const char *msg, int service)
{
	gint pos = 0;
	struct contact *con = NULL;
	eb_account *ac = NULL;

	if (service != -1)
		ac = find_account_by_handle(to, service);
	else
		con = find_contact_by_nick(to);

	if (ac) {
		con = ac->account_contact;
		ay_conversation_chat_with_account(ac);
	} else if (con) {
		ay_conversation_chat_with_contact(con);
	} else {
		return 0;
	}
	gtk_editable_insert_text(GTK_EDITABLE(con->conversation->window->entry),
		msg ? msg : "", msg ? strlen(msg) : 0, &pos);
	/* send_message(NULL, con->chatwindow); */
	return 1;
}

typedef struct _account_information {
	eb_account *ea;
	char *local_acc;
} account_information;

LList *ay_save_account_information(int service_id)
{
	LList *saved = NULL;
	LList *walk = NULL;

	for (walk = get_all_contacts(); walk; walk = walk->next) {
		struct contact *c = (struct contact *)walk->data;
		LList *accs = c->accounts;
		for (; accs; accs = accs->next) {
			eb_account *ea = (eb_account *)accs->data;
			if (service_id == -1 || ea->service_id == service_id) {
				account_information *ai =
					g_new0(account_information, 1);
				ai->ea = ea;
				if (ea->ela)
					ai->local_acc = strdup(ea->ela->handle);
				else
					ai->local_acc = NULL;
				eb_debug(DBG_CORE,
					" SAVED { %p(%s), %d, %s }\n", ea,
					ea->handle, ea->service_id,
					ea->ela ? ea->ela->handle : "NULL");
				saved = l_list_append(saved, ai);
			}
		}
	}
	return saved;
}

void ay_restore_account_information(LList *saved)
{
	LList *walk = NULL;

	for (walk = saved; walk; walk = walk->next) {
		account_information *ai = (account_information *)walk->data;
		eb_account *ea = ai->ea;
		if (ai->local_acc)
			ea->ela =
				find_local_account_by_handle(ai->local_acc,
				ea->service_id);
		else
			ea->ela = NULL;
		if (!ea->ela) {
			/* ooh an orphaned ea */
			ea->ela = find_local_account_for_remote(ea, 0);
			/* if still NULL, too bad. */
		}
		eb_debug(DBG_CORE, " RESTORED { %p(%s), %d, %s(%s) }\n", ea,
			ea->handle, ea->service_id, ai->local_acc,
			ea->ela ? ea->ela->handle : "NULL");
		free(ai->local_acc);
	}
}

void ay_dump_cts(void)
{
	LList *g = NULL, *c = NULL, *a = NULL;
	for (g = groups; g; g = g->next) {
		grouplist *gl = (grouplist *)g->data;
		printf("grouplist %p:\n"
			" name %s\n"
			" list_item %p\n"
			" tree %p\n"
			" label %p\n"
			" contacts_online %d\n"
			" contacts_shown %d\n"
			" members %p%s\n",
			gl,
			gl->name,
			gl->list_item,
			gl->tree,
			gl->label,
			gl->contacts_online,
			gl->contacts_shown,
			gl->members, gl->members ? ":" : "");

		for (c = gl->members; c; c = c->next) {
			struct contact *ct = (struct contact *)c->data;
			printf("  contact %p:\n"
				"   nick %s\n"
				"   language %s\n"
				"   trigger %p {%d, %d, %s}\n"
				"   expanded %d\n"
				"   online %d\n"
				"   send_offline %d\n"
				"   default_chatb %d\n"
				"   default_filetransb %d\n"
				"   group %p\n"
				"   chatwindow %p\n"
				"   logwindow %p\n"
				"   list_item %p\n"
				"   tree %p\n"
				"   pix %p\n"
				"   status %p\n"
				"   label %p\n"
				"   icon_handler %d\n"
				"   accounts %p%s\n",
				ct,
				ct->nick,
				ct->language,
				&ct->trigger,
				ct->trigger.type,
				ct->trigger.action,
				ct->trigger.param,
				ct->expanded,
				ct->online,
				ct->send_offline,
				ct->default_chatb,
				ct->default_filetransb,
				ct->group,
				ct->conversation,
				ct->logwindow,
				ct->list_item,
				ct->tree,
				ct->pix,
				ct->status,
				ct->label,
				ct->icon_handler,
				ct->accounts, ct->accounts ? ":" : "");
			for (a = ct->accounts; a; a = a->next) {
				eb_account *ea = (eb_account *)a->data;
				printf("    eb_account %p:\n"
					"     service_id %d\n"
					"     ela %p\n"
					"     handle %s\n"
					"     account_contact %p\n"
					"     protocol_account_data %p\n"
					"     list_item %p\n"
					"     status %p\n"
					"     pix %p\n"
					"     icon_handler %d\n"
					"     online %d\n"
					"     status_handler %d\n"
					"     info_window %p\n",
					ea,
					ea->service_id,
					ea->ela,
					ea->handle,
					ea->account_contact,
					ea->protocol_account_data,
					ea->list_item,
					ea->status,
					ea->pix,
					ea->icon_handler,
					ea->online,
					ea->status_handler, ea->infowindow);
			}
		}
	}
}

void ay_dump_elas()
{
	LList *la = NULL;
	for (la = accounts; la; la = la->next) {
		eb_local_account *ela = (eb_local_account *)la->data;
		printf("eb_local_account %p:\n"
			" service_id %d\n"
			" handle %s\n"
			" connected %d\n"
			" connecting %d\n"
			" status_button %p\n"
			" status_menu %p\n"
			" protocol_local_account_data %p\n"
			" mgmt_flush_tag %d\n"
			" connect_at_startup %d\n",
			ela,
			ela->service_id,
			ela->handle,
			ela->connected,
			ela->connecting,
			ela->status_button,
			ela->status_menu,
			ela->protocol_local_account_data,
			ela->mgmt_flush_tag, ela->connect_at_startup);
	}
}

/* taken from Yahoo's httplib */
int version_cmp(const char *v1, const char *v2)
{
	int x[2][4] = { {0, 0, 0, 0}, {0, 0, 0, 0} };
	const char *v[2] = { v1, v2 };
	const char *tmp, *tmp2;

	int i, j;

	for (i = 0; i < 2; i++) {
		tmp = v[i];
		for (j = 0; j < 4; j++) {
			if (!strncmp(tmp, "pre", strlen("pre")))
				x[i][j] = 0 - atoi(tmp + strlen("pre"));
			else
				x[i][j] = atoi(tmp);

			tmp2 = tmp;
			tmp = strchr(tmp, '.');
			if (tmp)
				tmp++;
			else {
				tmp = strchr(tmp2, '-');
				if (tmp)
					tmp++;
				else
					break;
			}
		}
	}

	for (j = 0; j < 4; j++)
		if (x[0][j] != x[1][j])
			return x[0][j] - x[1][j];

	return 0;
}

struct version_data {
	char *version;
	int tag;
	int input_tag;
	int done;
};

static void ay_got_version_data(AyConnection *fd, eb_input_condition condition, void *data)
{
	char *version = NULL;
	char buf[1480];
	int read_len = 0, len = 0;

	struct version_data *outversion = data;

	/* 
	 * Prefer to block here since our file is very small.
	 * This should ideally run only once unless something really
	 * nasty is happening.
	 */
	do {
		read_len = ay_connection_read(fd, buf+len, sizeof(buf)-1);
		if(read_len > 0) {
			char *offset = NULL;

			len += read_len;
			buf[len] = '\0';
			if((offset = strstr(buf, "-->"))) {
				*offset = '\0';
				version = strstr(buf, "<!--");
				version += 4;

				break;
			}
		}
		else if(read_len == 0)
			break;
	} while(read_len > 0 || errno == EAGAIN);

	if(version)
		outversion->version = strdup(version);

	ay_connection_input_remove(outversion->input_tag);
	ay_connection_free(fd);
	outversion->done = 1;
}

static void ay_got_version_connection(AyConnection *fd, int error, void *data)
{
	struct version_data *d = data;

	if (error || !fd) {
		if (error != AY_CANCEL_CONNECT)
			ay_do_error(_("Error!"),
				_("Unable to connection to SourceForge"));

		d->done = 1;
		return;
	}

	d->input_tag = ay_connection_input_add(fd, EB_INPUT_READ, ay_got_version_data, data);
	ay_connection_write(fd, "GET /release.php HTTP/1.0\r\n"
		"Host: ayttm.sourceforge.net\r\n"
		"User Agent: Ayttm\r\n"
		"\r\n", 77);
}

void ay_version_cancel_connect(void *data)
{
	struct version_data *d = data;
	ay_connection_cancel_connect(d->tag);
}

char *ay_get_last_version(void)
{
	AyConnection *con = NULL;
	int connect_tag = 0;
	char *version = NULL;
	struct version_data *data = g_new0(struct version_data, 1);

	con = ay_connection_new("ayttm.sourceforge.net", 80,
		AY_CONNECTION_TYPE_PLAIN);

	data->tag = ay_connection_connect(con, ay_got_version_connection, NULL,
		NULL, data);

	connect_tag = ay_activity_bar_add(
		_("Getting latest version information"),
		ay_version_cancel_connect, data);

	while(!data->done)
		gtk_main_iteration();

	ay_activity_bar_remove(connect_tag);

	version = data->version;

	g_free(data);

	return version;
}

/*
 * Tries to convert incoming string into utf-8
 */
gchar *convert_to_utf8(const char *message)
{
	const gchar *error_loc = NULL;
	gchar *output = NULL;
	gchar *correct = NULL;
	gchar *converted = NULL;
	const gchar *home_encoding = NULL;
	int i = 0;

	if (!message)
		return NULL;

	char **enclist = g_strsplit_set(cGetLocalPref("encodings"), " \t", -1);

	/* We need not do anything for a valid UTF-8 string */
	if (g_utf8_validate(message, -1, &error_loc))
		return g_strdup(message);

	/* The string is not utf-8. Save the valid result and 
	 * try converting from the error position onwards */
	correct = g_strndup(message, error_loc - message);

	/* Try converting from the encoding options configured */

	while (enclist[i] && enclist[i][0]) {
		converted =
			g_convert(error_loc, -1, "UTF-8", enclist[i], NULL,
			NULL, NULL);

		if (converted) {
			output = g_strjoin("", correct, converted, NULL);

			g_free(converted);
			g_free(correct);

			return output;
		}
		i++;
	}

	/* Nothing worked. Just convert from your locale to utf-8 with fallbacks.
	 * Unless of course, the locale is also UTF-8, hence making this a moot point */
	if (!g_get_charset(&home_encoding))
		converted =
			g_convert_with_fallback(error_loc, -1, "UTF-8",
			home_encoding, NULL, NULL, NULL, NULL);

	if (converted)
		output = g_strjoin("", correct, converted, NULL);
	else
		output = g_strjoin("", correct,
			_
			("<font color=\"#f00\">(truncated message since it was in an unknown encoding)</font>"),
			NULL);

	g_free(converted);
	g_free(correct);

	return output;
}

static const char base64_alpha[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/* Encode binary data into base64 */
char *ay_base64_encode(const unsigned char *in, int len)
{
	int i = 0, j = 0;

	unsigned char *out =
		(unsigned char *)calloc((len + len % 3) * 4 / 3 + 1,
		sizeof(unsigned char));

	for (i = 0, j = 0; i < len; i += 3, j += 4) {

		out[j] = base64_alpha[in[i] >> 2];

		if (i + 1 < len)
			out[j + 1] =
				base64_alpha[((in[i] & 0x03) << 4) | (in[i +
						1] >> 4)];
		else {
			out[j + 1] = base64_alpha[((in[i] & 0x03) << 4)];
			out[j + 2] = '=';
			out[j + 3] = '=';
		}

		if (i + 2 < len) {
			out[j + 2] =
				base64_alpha[((in[i + 1] & 0x0f) << 2) | (in[i +
						2] >> 6)];
			out[j + 3] = base64_alpha[((in[i + 2] & 0x3f))];
		} else if (i + 2 == len) {
			out[j + 2] = base64_alpha[((in[i + 1] & 0x0f) << 2)];
			out[j + 3] = '=';
		}
	}

	return (char *)out;
}

/* Decode base64 into (possibly) binary data */
unsigned char *ay_base64_decode(const char *in, int *len)
{
	int i = 0, j = 0;
	int inlen = strlen(in);
	int less = 0;

	if (in[inlen - 2] == '=')
		less = 2;
	else if (in[inlen - 1] == '=')
		less = 1;

	*len = (inlen * 3 / 4) - less;

	/* Leave space for null termination in case this is just a string */
	unsigned char *out = (unsigned char *)calloc(*len + 1, sizeof(unsigned char));

	for (i = 0, j = 0; i < inlen; i += 4, j += 3) {
		char tmpin[4] = { 0, 0, 0, 0 };
		int k = 0;

		for (k = 0; k < 65; k++) {
			if (in[i] == base64_alpha[k])
				tmpin[0] = k;

			if (in[i + 1] == base64_alpha[k])
				tmpin[1] = k;

			if (in[i + 2] == base64_alpha[k])
				tmpin[2] = k;

			if (in[i + 3] == base64_alpha[k])
				tmpin[3] = k;
		}

		out[j] = (tmpin[0] << 2) | ((tmpin[1] & 0x30) >> 4);

		if (tmpin[2] < 64) {
			out[j + 1] =
				((tmpin[1] & 0x0f) << 4) | ((tmpin[2] & 0x3c) >>
				2);
		}
		if (tmpin[3] < 64) {
			out[j + 2] = ((tmpin[2] & 0x03) << 6) | tmpin[3];
		}
	}

	return out;
}
