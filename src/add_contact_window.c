/*
* Ayttm 
*
* Copyright (C) 1999, Torrey Searle <tsearle@uci.edu>
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
#include <stdlib.h>

#include "service.h"
#include "util.h"
#include "gtk_globals.h"
#include "status.h"
#include "add_contact_window.h"
#include "messages.h"

#include <gdk/gdkkeysyms.h>

/*
* This is the GUI that gets created when you click on the "Add" button
* on the edit contacts tab, this is to add a new contact to your contact
* list
*/

static gint window_open = 0;
static GtkWidget *add_contact_window;
static GtkWidget *service_list;
static GtkWidget *account_name;
static GtkWidget *contact_name;
static GtkWidget *group_name;

static int contact_input_handler;

#define COMBO_TEXT(x) gtk_combo_box_get_active_text(GTK_COMBO_BOX(x))

static gint strcasecmp_glist(gconstpointer a, gconstpointer b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

/*
* This function just gets a linked list of all contacts associated, this 
* with a particular group this we use it to populate the contacts combo widget
* with this information
*/

static LList *get_contacts(const gchar *group)
{
	LList *node = NULL, *newlist = NULL;
	grouplist *g;

	g = find_grouplist_by_name(group);

	if (g)
		node = g->members;

	while (node) {
		newlist = l_list_insert_sorted(newlist,
			((struct contact *)node->data)->nick, strcasecmp_glist);
		node = node->next;
	}

	return newlist;
}

/* We ought to have something like this in gtk2 */
void gtk_combo_box_set_active_text(GtkComboBox *combo, gchar *nick,
	gint list_size)
{
	int i = 0;

	for (i = 0; i < list_size; i++) {
		char *cur;
		gtk_combo_box_set_active(combo, i);
		cur = gtk_combo_box_get_active_text(combo);

		if (cur && !strcmp(cur, nick))
			return;
	}
	gtk_combo_box_set_active(combo, -1);
}

LList *get_all_contacts()
{
	LList *node = get_groups();
	LList *newlist = NULL;

	while (node) {
		LList *g = get_contacts(node->data);
		newlist = l_list_concat(newlist, g);
		node = node->next;
	}

	return newlist;
}

/*
* this gets a list of all accounts associated with a contact
*/

static LList *get_eb_accounts(gchar *contact)
{
	LList *node = NULL, *newlist = NULL;
	struct contact *c;

	c = find_contact_by_nick(contact);

	if (c)
		node = c->accounts;

	while (node) {
		newlist = l_list_append(newlist, ((eb_account *)node->data));
		node = node->next;
	}

	return newlist;
}

LList *get_all_accounts(int serviceid)
{
	LList *node = get_all_contacts();
	LList *newlist = NULL;

	while (node) {
		LList *g =
			get_eb_accounts(((struct contact *)node->data)->nick);
		while (g) {
			eb_account *ac = (eb_account *)g->data;
			LList *next = g->next;

			if (ac->service_id == serviceid)
				newlist = l_list_append(newlist, ac->handle);

			free(g);
			g = next;
		}
		node = node->next;
	}

	return newlist;
}

static void dif_group(GtkWidget *widget, gpointer user_data)
{
	GList *gwalker = NULL;

	GList *list = llist_to_glist(get_contacts(COMBO_TEXT(group_name)), 1);

	const char *tmp =
		gtk_entry_get_text(GTK_ENTRY(GTK_BIN(contact_name)->child));

	if (g_signal_handler_is_connected(contact_name, contact_input_handler))
		g_signal_handler_block(contact_name, contact_input_handler);

	/* Clear the combo box. Sadly, we don't have a method for this */
	gtk_list_store_clear(GTK_LIST_STORE(gtk_combo_box_get_model
			(GTK_COMBO_BOX(contact_name))));

	list = g_list_prepend(list, (gpointer) (tmp ? tmp : ""));

	for (gwalker = list; gwalker; gwalker = g_list_next(gwalker))
		gtk_combo_box_append_text(GTK_COMBO_BOX(contact_name),
			(char *)gwalker->data);

	g_list_free(list);

	if (g_signal_handler_is_connected(contact_name, contact_input_handler))
		g_signal_handler_unblock(contact_name, contact_input_handler);
}

/*This is the function for changing the contact entry*/

static void set_con(GtkEditable *editable, gpointer user_data)
{
	g_signal_handler_block(GTK_COMBO_BOX(contact_name),
		contact_input_handler);
	gtk_entry_set_text(GTK_ENTRY(GTK_BIN(contact_name)->child),
		gtk_entry_get_text(GTK_ENTRY(account_name)));
	g_signal_handler_unblock(GTK_COMBO_BOX(contact_name),
		contact_input_handler);
}

/*callback that sets the flag if the contact name has been modified*/

static void con_modified(GtkEditable *editable, gpointer user_data)
{
	g_signal_handlers_disconnect_by_func(account_name, G_CALLBACK(set_con),
		NULL);
	g_signal_handlers_disconnect_by_func(contact_name,
		G_CALLBACK(con_modified), NULL);
}

static void add_button_callback(void)
{
	grouplist *gl;
	struct contact *con;
	gchar *service =
		gtk_combo_box_get_active_text(GTK_COMBO_BOX(service_list));
	const gchar *account = gtk_entry_get_text(GTK_ENTRY(account_name));
	gchar *mservice = NULL;
	gint service_id = -1;
	gchar *local_acc = strstr(service, " ") + 1;
	eb_local_account *ela = NULL;
	eb_account *ea = NULL;

	if (!strcmp(account, "")) {
		ay_do_error(_("Account error"),
			_("No Account Name specified."));
		return;
	}
	if (!strstr(service, "]") || !strstr(service, " ")) {
		ay_do_error(_("Account error"),
			_("No local account specified."));
		g_free(service);
		return;
	}

	*(strstr(service, "]")) = '\0';

	mservice = strstr(service, "[") + 1;
	service_id = get_service_id(mservice);
	ela = find_local_account_by_handle(local_acc, service_id);

	if (!ela) {
		ay_do_error(_("Account error"),
			_("Local account doesn't exist."));
		g_free(service);
		return;
	}

	ea = eb_services[service_id].sc->new_account(ela, account);

	if (!ea) {
		ay_do_error(_("Error"),
			_
			("Creation of this contact is impossible.\nThe service chosen is unavailable."));
		g_free(service);
		return;
	}
	ea->service_id = service_id;

	g_free(service);

	if (eb_services[service_id].sc->check_login) {
		char *buf = NULL;
		char *err =
			eb_services[service_id].sc->check_login((char *)account,
			(char *)"");
		if (err != NULL) {
			buf = g_strdup_printf(_
				("This account is not a valid %s account: \n\n %s"),
				get_service_name(service_id), err);
			g_free(err);
			ay_do_error(_("Invalid Account"), buf);
			g_free(buf);
			return;
		}
	}
	gl = find_grouplist_by_name(COMBO_TEXT(group_name));
	con = find_contact_by_nick(COMBO_TEXT(contact_name));

	if (!gl) {
		add_group(COMBO_TEXT(group_name));
		gl = find_grouplist_by_name(COMBO_TEXT(group_name));
	}

	if (!con) {
		con = add_new_contact(COMBO_TEXT(group_name),
			COMBO_TEXT(contact_name), ea->service_id);
	}

	add_account(con->nick, ea);
/*	update_contact_list ();
	write_contact_list(); */
}

/*
* Create a Add contact window and put it on the screen
*/

/* 
 * FIXME Someone please tell me why the third argument exists.
 * I'll remove it otherwise as it's not being used in any case.
 */
static void show_add_defined_contact_window(struct contact *cont,
	grouplist *grp, struct contact *con)
{
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *label;
	guint label_key;
	GtkWidget *table;
	GtkWidget *frame;
	GList *list;
	GList *gwalker = NULL;
	GtkWidget *dialog_content_area;

	LList *walk;

	if (window_open)
		return;

	if (!accounts) {
		ay_do_error(_("No Local Accounts"),
			_
			("Cannot add contacts. You have not added any chat accounts yet"));
		return;
	}

	table = gtk_table_new(4, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	vbox = gtk_vbox_new(FALSE, 5);
	hbox = gtk_hbox_new(FALSE, 0);

	/*Section for adding account */

	label = gtk_label_new_with_mnemonic(_("_Account: "));
	label_key = gtk_label_get_mnemonic_keyval(GTK_LABEL(label));
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), hbox, 0, 1, 0, 1, GTK_FILL, GTK_FILL,
		0, 0);
	gtk_widget_show(hbox);

	account_name = gtk_entry_new();
	if (cont == NULL)
		g_signal_connect(account_name, "changed", G_CALLBACK(set_con),
			NULL);

	gtk_table_attach(GTK_TABLE(table), account_name, 1, 2, 0, 1, GTK_FILL,
		GTK_FILL, 0, 0);
	gtk_widget_show(account_name);

	/*Section for declaring the protocol & local account */

	hbox = gtk_hbox_new(FALSE, 0);

	label = gtk_label_new_with_mnemonic(_("_Local account: "));
	label_key = gtk_label_get_mnemonic_keyval(GTK_LABEL(label));
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), hbox, 0, 1, 1, 2, GTK_FILL, GTK_FILL,
		0, 0);
	gtk_widget_show(hbox);

	/*List of Local accounts */

	service_list = gtk_combo_box_new_text();
	for (walk = accounts; walk; walk = walk->next) {
		eb_local_account *ela = (eb_local_account *)walk->data;
		if (ela) {
			char str[255];
			snprintf(str, sizeof(str), "[%s] %s",
				get_service_name(ela->service_id), ela->handle);

			gtk_combo_box_append_text(GTK_COMBO_BOX(service_list),
				str);
		}
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(service_list), 0);

	gtk_table_attach(GTK_TABLE(table), service_list, 1, 2, 1, 2, GTK_FILL,
		GTK_FILL, 0, 0);
	gtk_widget_show(service_list);

	/*Section for Contact Name */

	hbox = gtk_hbox_new(FALSE, 0);

	label = gtk_label_new_with_mnemonic(_("Contact: "));
	label_key = gtk_label_get_mnemonic_keyval(GTK_LABEL(label));
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), hbox, 0, 1, 2, 3, GTK_FILL, GTK_FILL,
		0, 0);
	gtk_widget_show(hbox);

	/*List of available contacts */

	contact_name = gtk_combo_box_entry_new_text();
	list = llist_to_glist(get_all_contacts(), 1);

	for (gwalker = list; gwalker; gwalker = g_list_next(gwalker))
		gtk_combo_box_append_text(GTK_COMBO_BOX(contact_name),
			(char *)gwalker->data);

	if (cont != NULL)
		gtk_combo_box_set_active_text(GTK_COMBO_BOX(contact_name),
			cont->nick, g_list_length(list));
	else
		gtk_combo_box_set_active(GTK_COMBO_BOX(contact_name), -1);
	g_list_free(list);

	contact_input_handler =
		g_signal_connect(contact_name, "changed",
		G_CALLBACK(con_modified), NULL);
	gtk_table_attach(GTK_TABLE(table), contact_name, 1, 2, 2, 3, GTK_FILL,
		GTK_FILL, 0, 0);
	gtk_widget_show(contact_name);

	/*Section for Group declaration */

	hbox = gtk_hbox_new(FALSE, 0);

	label = gtk_label_new_with_mnemonic(_("_Group: "));
	label_key = gtk_label_get_mnemonic_keyval(GTK_LABEL(label));
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), hbox, 0, 1, 3, 4, GTK_FILL, GTK_FILL,
		0, 0);
	gtk_widget_show(hbox);

	/*List of available groups */

	group_name = gtk_combo_box_entry_new_text();
	list = llist_to_glist(get_groups(), 1);

	for (gwalker = list; gwalker; gwalker = g_list_next(gwalker))
		gtk_combo_box_append_text(GTK_COMBO_BOX(group_name),
			(char *)gwalker->data);

	if (cont != NULL)
		gtk_combo_box_set_active_text(GTK_COMBO_BOX(group_name),
			cont->group->name, g_list_length(list));
	else if (grp != NULL)
		gtk_combo_box_set_active_text(GTK_COMBO_BOX(group_name),
			grp->name, g_list_length(list));
	else
		gtk_entry_set_text(GTK_ENTRY(GTK_BIN(group_name)->child),
			_("Buddies"));

	g_list_free(list);

	g_signal_connect(group_name, "changed", G_CALLBACK(dif_group), NULL);
	gtk_table_attach(GTK_TABLE(table), group_name, 1, 2, 3, 4, GTK_FILL,
		GTK_FILL, 0, 0);
	gtk_widget_show(group_name);

	/*Lets create a frame to put all of this in */

	frame = gtk_frame_new(NULL);
	gtk_frame_set_label(GTK_FRAME(frame), _("Add Contact"));

	gtk_container_add(GTK_CONTAINER(frame), table);

	add_contact_window =
		gtk_dialog_new_with_buttons(_("Ayttm - Add Contact"),
		GTK_WINDOW(statuswindow),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT, GTK_STOCK_CANCEL,
		GTK_RESPONSE_REJECT, NULL);

	gtk_dialog_set_default_response(GTK_DIALOG(add_contact_window),
		GTK_RESPONSE_ACCEPT);

#ifdef HAVE_GTK_2_14
	dialog_content_area =
		gtk_dialog_get_content_area(GTK_DIALOG(add_contact_window));
#else
	dialog_content_area = GTK_DIALOG(add_contact_window)->vbox;
#endif

	gtk_box_pack_start(GTK_BOX(dialog_content_area), frame, TRUE, TRUE, 5);

	gtk_widget_grab_focus(account_name);

	gtk_widget_show_all(add_contact_window);
	window_open = 1;

	if (GTK_RESPONSE_ACCEPT ==
		gtk_dialog_run(GTK_DIALOG(add_contact_window)))
		add_button_callback();

	gtk_widget_destroy(add_contact_window);

	window_open = 0;
}

void show_add_contact_window()
{
	show_add_defined_contact_window(NULL, NULL, NULL);
}

void show_add_contact_to_group_window(grouplist *g)
{
	show_add_defined_contact_window(NULL, g, NULL);
}

void show_add_group_window()
{
	edit_group_window_new(NULL);
}

void show_add_account_to_contact_window(struct contact *cont)
{
	show_add_defined_contact_window(cont, NULL, NULL);
}
