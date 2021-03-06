/*
 * EveryBuddy 
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __MINGW32__
#define __IN_PLUGIN__ 1
#endif
#include "service.h"
#include "prefs.h"
#include "util.h"
#include "plugin_api.h"
#include "messages.h"

/*************************************************************************************
 *                             Begin Module Code
 ************************************************************************************/
/*  Module defines */
#ifndef USE_POSIX_DLOPEN
#define plugin_info import_gaim_LTX_plugin_info
#define plugin_init import_gaim_LTX_plugin_init
#define plugin_finish import_gaim_LTX_plugin_finish
#define module_version import_gaim_LTX_module_version
#endif

/* Function Prototypes */
void import_gaim_accounts(ebmCallbackData *data);
int plugin_init();
int plugin_finish();

static int ref_count = 0;

/*  Module Exports */
PLUGIN_INFO plugin_info = {
	PLUGIN_IMPORTER,
	"Gaim Buddy List",
	"Imports your Gaim buddy list into Ayttm",
	"$Revision: 1.12 $",
	"$Date: 2009/09/17 12:04:58 $",
	&ref_count,
	plugin_init,
	plugin_finish
};

/* End Module Exports */

static void *buddy_list_tag = NULL;

unsigned int module_version()
{
	return CORE_VERSION;
}

int plugin_init()
{
	eb_debug(DBG_MOD, "Gaim Buddy List init\n");
	buddy_list_tag =
		eb_add_menu_item("Gaim Buddy List", EB_IMPORT_MENU,
		import_gaim_accounts, ebmIMPORTDATA, NULL);
	if (!buddy_list_tag)
		return (-1);
	return (0);
}

int plugin_finish()
{
	int result;

	result = eb_remove_menu_item(EB_IMPORT_MENU, buddy_list_tag);
	if (result) {
		g_warning
			("Unable to remove Gaim Buddy List menu item from import menu!");
		return (-1);
	}
	return (0);
}

/*******************************************************************************
 *                             End Module Code
 ******************************************************************************/

void import_gaim_accounts(ebmCallbackData *data)
{
	gchar buff[1024];
	gchar c[1024];
	gchar group[1024];
	gchar *fname = NULL;
	gchar *handle = NULL;
	FILE *fp;

	gint AIM_ID = -1;
	g_snprintf(buff, 1024, "%s/gaim.buddy", getenv("HOME"));
	if (!(fp = fopen(buff, "r"))) {
		g_snprintf(c, 1024,
			"Unable to import gaim accounts from %s: %s", buff,
			strerror(errno));
		ay_do_error("Import Error", c);
		return;
	}
	AIM_ID = get_service_id("AIM");
	while (!feof(fp)) {
		fgets(c, 1024, fp);
		g_strchomp(c);
		if (*c == 'g') {
			strncpy(group, c + 2, 1024);
			if (!find_grouplist_by_name(group)) {
				add_group(group);
			}
		} else if (*c == 'b') {
			handle = c + 2;
			if (strchr(handle, ':')) {
				fname = strchr(handle, ':') + 1;
				*(strchr(handle, ':')) = '\0';
			} else
				fname = handle;

			if (!find_account_by_handle(handle, AIM_ID)) {
				eb_account *ea =
					eb_services[AIM_ID].sc->
					new_account(NULL, handle);
				add_account(fname, ea);
				move_contact(group,
					find_contact_by_nick(fname));
			}
		} else if (*c == 'p') {
			/*no need */
		} else if (*c == 'd') {
			/*no need */
		} else if (*c == 'm') {
			/*no need */
		}
	}
	fclose(fp);
	ay_do_info("Import", "Successfully imported gaim BuddyList");
}
