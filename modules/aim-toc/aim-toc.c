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

/*
 * aim.c
 * AIM implementation
 */
#ifdef __MINGW32__
#define __IN_PLUGIN__ 1
#endif

#include "intl.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if defined( _WIN32 ) && !defined(__MINGW32__)
#include "../libtoc/libtoc.h"
typedef unsigned long u_long;
typedef unsigned long ulong;
#else
#include "libtoc/libtoc.h"
#endif
#include "aim.h"
#include "away_window.h"
#include "util.h"
#include "status.h"
#include "dialog.h"
#include "progress_window.h"
#include "message_parse.h"
#include "value_pair.h"
#include "info_window.h"
#include "plugin_api.h"
#include "smileys.h"
#include "globals.h"
#include "pixmaps/aim_online.xpm"
#include "pixmaps/aim_away.xpm"
#ifdef __MINGW32__
#define snprintf _snprintf
#endif

#define DBG_TOC do_aim_debug
int do_aim_debug = 1;

/*************************************************************************************
 *                             Begin Module Code
 ************************************************************************************/
/*  Module defines */
#define plugin_info aim_toc_LTX_plugin_info
#define SERVICE_INFO aim_toc_LTX_SERVICE_INFO
#define plugin_init aim_toc_LTX_plugin_init
#define plugin_finish aim_toc_LTX_plugin_finish

/* Function Prototypes */
static int plugin_init();
static int plugin_finish();

static int ref_count = 0;
static char aim_server[MAX_PREF_LEN] = "toc.oscar.aol.com";
static char aim_port[MAX_PREF_LEN] = "80";

/*  Module Exports */
PLUGIN_INFO plugin_info = {
	PLUGIN_SERVICE,
	"AIM TOC Service",
	"AOL Instant Messenger support via the TOC protocol",
	"$Revision: 1.1.1.1 $",
	"$Date: 2003/04/01 07:24:26 $",
	&ref_count,
	plugin_init,
	plugin_finish
};
struct service SERVICE_INFO = { "AIM", -1, FALSE, TRUE, FALSE, TRUE, NULL };
/* End Module Exports */

static char *eb_toc_get_color(void) { static char color[]="#000088"; return color; }

static int plugin_init()
{
	input_list * il = g_new0(input_list, 1);
	eb_debug(DBG_MOD, "aim-toc\n");
	ref_count=0;
	plugin_info.prefs = il;
	il->widget.entry.value = aim_server;
	il->widget.entry.name = "aim_server";
	il->widget.entry.label = _("Server:");
	il->type = EB_INPUT_ENTRY;

        il->next = g_new0(input_list, 1);
	il = il->next;
	il->widget.entry.value = aim_port;
	il->widget.entry.name = "aim_port";
	il->widget.entry.label = _("Port:");
	il->type = EB_INPUT_ENTRY;

	il->next = g_new0(input_list, 1);
       	il = il->next;
       	il->widget.checkbox.value = &do_aim_debug;
       	il->widget.checkbox.name = "do_aim_debug";
       	il->widget.checkbox.label = _("Enable debugging");
       	il->type = EB_INPUT_CHECKBOX;

/*
	il->next = g_new0(input_list, 1);
	il = il->next;
	il->widget.entry.value = aim_info;
	il->widget.entry.name = "aim_info";
	il->widget.entry.label = _("Info:");
	il->type = EB_INPUT_ENTRY;
*/
        return(0);
}

static int plugin_finish()
{
	eb_debug(DBG_MOD, "Returning the ref_count: %i\n", ref_count);
	return(ref_count);
}

/*************************************************************************************
 *                             End Module Code
 ************************************************************************************/

struct eb_aim_account_data {
		int status;
        time_t idle_time;
        int logged_in_time;
	int evil;
};

struct eb_aim_local_account_data {
	char aim_info[MAX_PREF_LEN]; 
        char password[255];
        int fd;
	toc_conn * conn;
	int input;
	int keep_alive;
	int status;
	int connect_tag;
};

enum
{
	AIM_ONLINE=0,
	AIM_AWAY=1,
	AIM_OFFLINE=2
};


typedef struct _eb_aim_file_request
{
	toc_conn * conn;
	char nick[255];
	char ip[255];
	short port;
	char cookie[255];
	char filename[255];
} eb_aim_file_request;

/* here is the list of locally stored buddies */

static LList * aim_buddies;

/*	There are no prefs for AIM-TOC at the moment.
static input_list * aim_prefs = NULL;
*/

static eb_account * eb_aim_new_account( const char * account );
static void eb_aim_add_user( eb_account * account );
static void eb_aim_login( eb_local_account * account );
static void eb_aim_logout( eb_local_account * account );
static void aim_info_data_cleanup(info_window * iw);
static void aim_info_update(eb_account *sender);


/*********
 * the following variable is a hack, it if we are changing the selection
 * don't let the corresponding set_current_state get called again
 */

static char * eb_aim_check_login(char * user, char * pass)
{
	return NULL;
}


static int is_setting_state = 0;

static eb_local_account * aim_find_local_account_by_conn(toc_conn * conn)
{
	LList * node;
	for( node = accounts; node; node = node->next )
	{
		eb_local_account * ela = (eb_local_account *)node->data;
		if(ela->service_id == SERVICE_INFO.protocol_id) 
		{
			struct eb_aim_local_account_data * alad = (struct eb_aim_local_account_data *)ela->protocol_local_account_data;
		    if( alad->conn == conn )
			{
				return ela;
			}
		}
	}
	return NULL;
}

static void aim_set_profile_callback(char * value, void * data)
{
	eb_local_account * account = (eb_local_account*)data;
	struct eb_aim_local_account_data * alad 
		= (struct eb_aim_local_account_data *)account->protocol_local_account_data;

	strncpy(alad->aim_info, value, MAX_PREF_LEN);
	write_account_list();
}

static void aim_set_profile_window(ebmCallbackData *data)
{
	eb_local_account * account = NULL;
	struct eb_aim_local_account_data * alad;

	char buff[256];
	if(IS_ebmProfileData(data))
		account = (eb_local_account*)data->user_data;
	else 
	{	/* This should never happen, unless something is horribly wrong */
		fprintf(stderr, "data->CDType %d\n", data->CDType);
		fprintf(stderr, "Error! not of profile type!\n");
		return;
	}
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;

	g_snprintf(buff, 256, _("Profile for account %s"), account->handle); 
	do_text_input_window(buff, alad->aim_info, aim_set_profile_callback, account); 
}

static void eb_aim_disconnect( toc_conn * conn )
{
	eb_local_account * ela = conn->account;
	eb_debug(DBG_TOC, "eb_aim_disconnect %d %d\n", conn->fd, conn->seq_num);
	if(ela) {
		eb_aim_logout(ela);
	}
	else
		g_warning("NULL account associated with AIM connection");
}

static void eb_aim_join_ack(toc_conn * conn, char * id, char * name)
{
	eb_chat_room * ecr = find_chat_room_by_name(name, SERVICE_INFO.protocol_id);

	eb_debug(DBG_TOC, "eb_aim_join_ack %s %s\n", id, name );

	if(!ecr)
		return;
	
	eb_debug(DBG_TOC, "Match found, copying id!!");

	strncpy( ecr->id, id , sizeof(ecr->id));

	eb_join_chat_room(ecr);
}

static void eb_aim_chat_update_buddy(toc_conn * conn, char * id, 
		                      char * user, int online )
{
	eb_chat_room * ecr = find_chat_room_by_id(id);
	if(!ecr)
	{
			fprintf(stderr, "Error: unable to fine the chat room!!!\n" );
	}
	if(online)
	{
		eb_account * ea = find_account_by_handle(user, SERVICE_INFO.protocol_id);
		if( ea)
		{
			eb_chat_room_buddy_arrive(ecr, ea->account_contact->nick, user );
		}
		else
		{
			eb_chat_room_buddy_arrive(ecr, user, user);
		}
	}
	else
	{
		eb_chat_room_buddy_leave(ecr, user);
	}
}


/*the callback to call all callbacks :P */

static void eb_aim_callback(void *data, int source, eb_input_condition condition )
{
	struct eb_aim_local_account_data * alad = data;
	toc_conn * conn = alad->conn;
	eb_debug(DBG_TOC, "eb_aim_callback %d %d\n", conn->fd, conn->seq_num);
	if(source < 0 )
	{
		//eb_input_remove(*((int*)data));
		g_assert(0);
	}
	toc_callback(((struct eb_aim_local_account_data *)data)->conn);

}

static int eb_aim_keep_alive(gpointer data )
{
	struct eb_aim_local_account_data * alad = data;
	toc_conn * conn = alad->conn;
	eb_debug(DBG_TOC, "eb_aim_keep_alive %d %d\n", conn->fd, conn->seq_num);
	toc_send_keep_alive(alad->conn);
	return TRUE;
}

static void eb_aim_process_file_request( gpointer data, int result )
{
	int accepted = result;
	eb_aim_file_request * eafr = data;

	if(accepted)
	{
		toc_file_accept( eafr->conn, eafr->nick, eafr->ip, eafr->port,
						 eafr->cookie, eafr->filename );
		g_free(eafr);
	}
	else
	{
		toc_file_cancel( eafr->conn, eafr->nick, eafr->cookie );
		g_free(eafr);
	}
}

static void eb_aim_file_offer(toc_conn * conn, char * nick, char * ip, short port,
							  char * cookie, char * filename )
{
	eb_aim_file_request * eafr = g_new0( eb_aim_file_request, 1);
	char message[1024];

	eafr->conn = conn;
	strncpy( eafr->nick, nick, 255);
	strncpy( eafr->ip,  ip, 255);
	eafr->port = port;
	strncpy( eafr->filename, filename, 255);
	strncpy( eafr->cookie, cookie, 255 );

	g_snprintf( message, 1024, _("AIM user %s would like to\nsend you the file\n%s\ndo you want to accept?"), nick, filename );

	eb_do_dialog( message, _("Incoming AIM File Request"), eb_aim_process_file_request, eafr );

}


static void eb_aim_chat_invite(toc_conn * conn, char * id, char * name,
		                char * sender, char * message )
{
	eb_chat_room * chat_room = g_new0(eb_chat_room, 1);
  	eb_local_account * ela = aim_find_local_account_by_conn(conn);
	strncpy(chat_room->id, id, sizeof(chat_room->id));
	strncpy(chat_room->room_name, name, sizeof(chat_room->room_name));
	chat_room->connected = FALSE;
	chat_room->fellows = NULL;
	chat_room->protocol_local_chat_room_data = NULL; /* not needed for AIM */
	
	chat_room->local_user =  aim_find_local_account_by_conn(conn);

	invite_dialog( ela, sender, name, strdup(id) );

}


static void eb_aim_error_message( char * message )
{
	do_message_dialog(message, _("AIM Error"), 0);
}
	
static void eb_aim_oncoming_buddy(char * user, int online, time_t idle, int evil, int unavailable )
{
	eb_account * ea = find_account_by_handle( user, SERVICE_INFO.protocol_id);
	struct eb_aim_account_data * aad ;
	
	if(ea)
	{
		aad = ea->protocol_account_data;
		if (!l_list_find(aim_buddies, ea->handle))
			aim_buddies = l_list_append(aim_buddies, ea->handle);
	}
	else
	{
		return;
	}


	if (online && (aad->status == AIM_OFFLINE))
	{
		aad->status = AIM_ONLINE;
		buddy_login(ea);
	}
	else if(!online && (aad->status != AIM_OFFLINE))
	{
		aad->status = AIM_OFFLINE;
		buddy_logoff(ea);
	}

	if (online && unavailable)
		aad->status = AIM_AWAY;
	else if (online)
		aad->status = AIM_ONLINE;

	aad->evil = evil;
	aad->idle_time = idle;
	buddy_update_status(ea);
}

/* This is how we deal with incomming chat room messages */

static void eb_aim_toc_chat_im_in( toc_conn * conn, char * id, char * user, char * message )
{
	eb_chat_room * ecr = find_chat_room_by_id( id );
	eb_account * ea = find_account_by_handle(user, SERVICE_INFO.protocol_id);
	char * message2 = linkify(message);

	if(!ecr)
	{
		g_warning("Chat room does not Exist!!!");
		g_free(message2);
		return;
	}

	if( ea)
	{
		eb_chat_room_show_message( ecr, ea->account_contact->nick, message2 );
	}
	else
	{
		eb_chat_room_show_message( ecr, user, message2 );
	}
	g_free(message2);
}

static void eb_aim_user_info(toc_conn * conn, char * user, char * message )
{
	eb_local_account * ela =  aim_find_local_account_by_conn(conn);
	eb_account * sender = NULL;
	eb_local_account * reciever = NULL;


	sender = find_account_by_handle(user, ela->service_id);
	if(sender==NULL)
	{
		eb_account * ea = g_new0(eb_account, 1);
		struct eb_aim_account_data * aad = g_new0(struct eb_aim_account_data, 1);
		strncpy(ea->handle, user, 255);
		ea->service_id = ela->service_id;
		aad->status = AIM_OFFLINE;
		ea->protocol_account_data = aad;

		add_unknown(ea);
		sender = ea;

	}
	reciever = find_suitable_local_account( ela, ela->service_id );

	if(sender->infowindow == NULL )
	{
		sender->infowindow = eb_info_window_new(reciever, sender);
	}

	sender->infowindow->info_data = strdup(message);
	sender->infowindow->cleanup = aim_info_data_cleanup;
	aim_info_update(sender);
}

static void eb_aim_new_group(char * group)
{
	if(!find_grouplist_by_name(group)) {
		add_group(group);
	}
}

static void eb_aim_new_user(char * group, char * handle)
{
	eb_account * ea = find_account_by_handle( handle, SERVICE_INFO.protocol_id );

	if(!ea)
	{
		grouplist * gl = find_grouplist_by_name(group);
		struct contact * c = find_contact_by_nick(handle);
		ea = eb_aim_new_account(handle);
	

		if(!gl && !c)
		{
			add_group(group);
		}
		if(!c)
		{
			c = add_new_contact(group, handle, SERVICE_INFO.protocol_id);
		}

		ea->list_item = NULL;
		ea->online = 0;
		ea->status = NULL;
		ea->pix = NULL;
		ea->icon_handler = -1;
		ea->status_handler = -1;
	
		aim_buddies = l_list_append(aim_buddies, handle);
		c->accounts = l_list_append(c->accounts, ea);
		ea->account_contact = c;
#if 0
		add_account(handle, ea);
#endif

		update_contact_list ();
		write_contact_list();
	}
}
		


	

static void eb_aim_parse_incoming_im(toc_conn * conn, char * user, char * message )
{
	    //time_t  t = 0;
  		eb_local_account * ela = aim_find_local_account_by_conn(conn);
		struct eb_aim_local_account_data * alad = ela->protocol_local_account_data;
		
		eb_account * sender = NULL;
		eb_local_account * reciever = NULL;

		eb_debug(DBG_TOC, "eb_aim_parse_incomming_im %d %d, %d %d\n", conn->fd, conn->seq_num, alad->conn->fd, alad->conn->seq_num );

		sender = find_account_by_handle(user, ela->service_id);
		if(sender==NULL)
		{
			eb_account * ea = g_new0(eb_account, 1);
			struct eb_aim_account_data * aad = g_new0(struct eb_aim_account_data, 1);
			strncpy(ea->handle, user, 255);
			ea->service_id = ela->service_id;
			aad->status = AIM_OFFLINE;
			ea->protocol_account_data = aad;
			
			add_unknown(ea);
			//aim_add_buddy(command->conn,screenname);
			sender = ea;

			eb_debug(DBG_TOC, "Sender == NULL");
		}
		if (sender && !sender->online) {
			/* that's impossible if he talks to us */
			toc_add_buddy(conn,sender->handle,
					sender->account_contact->group->name);
		}
		
		reciever = find_suitable_local_account( ela, ela->service_id);
		//strip_html(msg);

		eb_parse_incoming_message(reciever, sender, message);
		if(reciever == NULL)
		{
			g_warning("Reciever == NULL");
		}

        eb_debug(DBG_TOC, "%s %s\n", user, message);
		return;
}



/*   callbacks used by EveryBuddy    */
static int eb_aim_query_connected(eb_account * account)
{		
	struct eb_aim_account_data * aad = account->protocol_account_data;

	if(ref_count <= 0 )
		aad->status = AIM_OFFLINE;
	return aad->status != AIM_OFFLINE;
}

static void eb_aim_accept_invite( eb_local_account * account, void * invitation )
{
	char * id = invitation;
	eb_chat_room * chat_room = find_chat_room_by_id( id );

	struct eb_aim_local_account_data * alad = 
						account->protocol_local_account_data;
	
	toc_conn * conn = alad->conn;

	toc_chat_accept(conn, id);
	eb_join_chat_room( chat_room);
	free(id);
}

static void eb_aim_decline_invite( eb_local_account * account, void * invitation )
{
	char * id = invitation;
	free( id );
}


static void eb_aim_send_chat_room_message( eb_chat_room * room, char * message )
{
	struct eb_aim_local_account_data * alad = room->local_user->protocol_local_account_data;
	toc_conn * conn = alad->conn;
	char * message2 = linkify(message);

	toc_chat_send(conn, room->id, message2 );
	g_free(message2);
}

static void eb_aim_join_chat_room( eb_chat_room * room )
{
	struct eb_aim_local_account_data * alad = room->local_user->protocol_local_account_data;
	toc_conn * conn = alad->conn;
	toc_chat_join(conn, room->room_name);
}

static void eb_aim_leave_chat_room( eb_chat_room * room )
{
	struct eb_aim_local_account_data * alad = room->local_user->protocol_local_account_data;
	toc_conn * conn = alad->conn;
	toc_chat_leave(conn, room->id);
}

static eb_chat_room * eb_aim_make_chat_room(char * name, eb_local_account * account )
{
	eb_chat_room * ecr = g_new0(eb_chat_room, 1);

	strncpy( ecr->room_name, name , sizeof(ecr->room_name));
	ecr->fellows = NULL;
	ecr->connected = FALSE;
	ecr->local_user = account;
	eb_join_chat_room(ecr);
	

	return ecr;
}

static eb_account * eb_aim_new_account( const char * account )
{
	eb_account * a = g_new0(eb_account, 1);
	struct eb_aim_account_data * aad = g_new0(struct eb_aim_account_data, 1);

	a->protocol_account_data = aad;
	strncpy(a->handle, account, 255);
	a->service_id = SERVICE_INFO.protocol_id;
	aad->status = AIM_OFFLINE;

	return a;
}

static void eb_aim_del_user( eb_account * account )
{
	LList * node;
	assert( eb_services[account->service_id].protocol_id == SERVICE_INFO.protocol_id );
	for( node = accounts; node; node=node->next )
	{
		eb_local_account * ela = node->data;
		if( ela->connected && ela->service_id == account->service_id)
		{
			struct eb_aim_local_account_data * alad = ela->protocol_local_account_data;
			toc_remove_buddy(alad->conn,account->handle,
					account->account_contact->group->name);
		}
	}
}

static void eb_aim_add_user( eb_account * account )
{
	LList * node;

	assert( eb_services[account->service_id].protocol_id == SERVICE_INFO.protocol_id );
	aim_buddies = l_list_append(aim_buddies, account->handle);

	for( node = accounts; node; node=node->next )
	{
		eb_local_account * ela = node->data;
		if( ela && ela->connected && ela->service_id == account->service_id)
		{
			struct eb_aim_local_account_data * alad = ela->protocol_local_account_data;
			toc_add_buddy(alad->conn,account->handle,
					account->account_contact->group->name);
		}
		
	}
}


static void eb_aim_login( eb_local_account * account )
{
	struct eb_aim_local_account_data * alad;
	char buff[1024];
	snprintf(buff, sizeof(buff), _("Logging in to AIM account: %s"), account->handle);

	account->connecting = 1;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;

	/*alad->connect_tag = activity_window_new(buff);*/
	
	toc_signon( account->handle, alad->password,
			      aim_server, atoi(aim_port), alad->aim_info);
	
	progress_window_close (alad->connect_tag);
}

static void eb_aim_logged_in (toc_conn *conn)
{
	struct eb_aim_local_account_data * alad;
	eb_local_account *ela = NULL;
	
	if (!conn)
		return;
	
	ela = find_local_account_by_handle(conn->username, SERVICE_INFO.protocol_id);
	alad = (struct eb_aim_local_account_data *)ela->protocol_local_account_data;
	alad->conn = conn;
	
	if(!alad->conn)
	{
		g_warning("FAILED TO CONNECT TO AIM SERVER!!!!!!!!!!!!");
		eb_aim_logout(ela);
		return;
	}
	if(alad->conn->fd == -1 )
	{
		g_warning("eb_aim UNKNOWN CONNECTION PROBLEM");
		eb_aim_logout(ela);
		return;
	}
	eb_debug(DBG_TOC, "eb_aim_login %d %d\n", alad->conn->fd, alad->conn->seq_num );
	alad->conn->account = ela;
	alad->status = AIM_ONLINE;
	ref_count++;
	alad->input = eb_input_add(alad->conn->fd, EB_INPUT_READ, eb_aim_callback, alad);
		
	alad->keep_alive = eb_timeout_add((guint32)60000, eb_aim_keep_alive, (gpointer)alad );

	is_setting_state = 1;
	
	if(ela->status_menu)
		eb_set_active_menu_status(ela->status_menu, AIM_ONLINE);

	is_setting_state = 0;
	ela->connecting = 0;
	ela->connected = 1;
	
	toc_add_buddy(alad->conn,ela->handle,
			"Unknown");
	aim_buddies = l_list_append(aim_buddies, ela->handle);
								  
}

static void eb_aim_send_invite( eb_local_account * account, eb_chat_room * room,
						 char * user, char * message)
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;
	toc_invite( alad->conn, room->id, user, message );
}
	

static void eb_aim_logout( eb_local_account * account )
{
	LList *l;
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;
	eb_debug(DBG_TOC, "eb_aim_logout %d %d\n", alad->conn->fd, alad->conn->seq_num );
	eb_input_remove(alad->input);
	eb_timeout_remove(alad->keep_alive);
	if(alad->conn)
	{
		toc_signoff(alad->conn);
		g_free(alad->conn);
		alad->conn = NULL;
	}
	else
	{
		return;
	}
#if 0
	if(account->status_menu)
		eb_set_active_menu_status(account->status_menu, AIM_ONLINE);
#endif
	alad->status=AIM_OFFLINE;
	ref_count--;
	account->connected = 0;

	is_setting_state = 1;

	if(account->status_menu)
		eb_set_active_menu_status(account->status_menu, AIM_OFFLINE);

	is_setting_state = 0;
	
	/* Make sure each AIM buddy gets logged off from the status window */
	for(l = aim_buddies; l; l=l->next)
	{
		eb_aim_oncoming_buddy(l->data, FALSE, 0, 0, FALSE);
	}


}

static void eb_aim_send_im( eb_local_account * account_from,
				  eb_account * account_to,
				  char * message )
{
	struct eb_aim_local_account_data * plad = (struct eb_aim_local_account_data*)account_from->protocol_local_account_data;
	char * message2 = linkify(message);
	if(strlen(message2) > 2000)
	{
		do_message_dialog(_("Message Truncated"), _("AIM Error"), 0);
		message2[2000] = '\0';
	}
	toc_send_im(plad->conn,account_to->handle, message2);
	eb_debug(DBG_TOC, "eb_aim_send_im %d %d\n", plad->conn->fd, plad->conn->seq_num);
	eb_debug(DBG_TOC, "eb_aim_send_im %s", message);

	g_free(message2);
}
		
static eb_local_account * eb_aim_read_local_config(LList * pairs)
{

	eb_local_account * ela = g_new0(eb_local_account, 1);
	char * c = NULL;
	char buff[1024];
	struct eb_aim_local_account_data * ala = g_new0(struct eb_aim_local_account_data, 1);
	strncpy(ala->aim_info,  "Visit the Ayttm website at <a href=\"http://www.nongnu.org/ayttm/\">www.nongnu.org/ayttm</a>",
			sizeof(ala->aim_info));

	
	eb_debug(DBG_TOC, "eb_aim_read_local_config: entering\n");	
	/*you know, eventually error handling should be put in here*/
	ela->handle=strdup(value_pair_get_value(pairs, "SCREEN_NAME"));
	strncpy(ela->alias, ela->handle, 255);
	strncpy(ala->password, value_pair_get_value(pairs, "PASSWORD"), 255);


	c = value_pair_get_value(pairs, "PROFILE");
	if(c) {	
		strncpy(ala->aim_info, c, MAX_PREF_LEN);
	}

	snprintf(buff, sizeof(buff), "%s [AIM]", ela->alias);
	eb_add_menu_item(strdup(buff), EB_PROFILE_MENU, aim_set_profile_window, ebmPROFILEDATA, ebmProfileData_new(ela));

    ela->service_id = SERVICE_INFO.protocol_id;
    ela->protocol_local_account_data = ala;
	ala->status = AIM_OFFLINE;
	eb_debug(DBG_TOC, "eb_aim_read_local_config: returning %p\n", ela);

    return ela;
}

static LList * eb_aim_write_local_config( eb_local_account * account )
{
	LList * list = NULL;
	struct eb_aim_local_account_data * alad = account->protocol_local_account_data; 

	list = value_pair_add(list, "SCREEN_NAME", account->handle);
	list = value_pair_add(list, "PASSWORD", alad->password);
	list = value_pair_add(list, "PROFILE", alad->aim_info);

	return list;
}
			

	

static eb_account * eb_aim_read_config( LList * config, struct contact *contact )
{
    eb_account * ea = g_new0(eb_account, 1 );
    struct eb_aim_account_data * aad =  g_new0(struct eb_aim_account_data,1);
	
	aad->status = AIM_OFFLINE;

    /*you know, eventually error handling should be put in here*/
    strncpy(ea->handle, value_pair_get_value( config, "NAME"), 255);

    ea->service_id = SERVICE_INFO.protocol_id;
    ea->protocol_account_data = aad;
    ea->account_contact = contact;
	ea->list_item = NULL;
	ea->online = 0;
	ea->status = NULL;
	ea->pix = NULL;
	ea->icon_handler = -1;
	ea->status_handler = -1;
	
	eb_aim_add_user(ea);

    return ea;
}

static LList * eb_aim_get_states()
{
	LList * states = NULL;
	states = l_list_append(states, "Online");
	states = l_list_append(states, "Away");
	states = l_list_append(states, "Offline");
	
	return states;
}

static int eb_aim_get_current_state(eb_local_account * account )
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;
	eb_debug(DBG_TOC, "eb_aim_get_current_state: %i %i\n", eb_services[account->service_id].protocol_id, SERVICE_INFO.protocol_id);	
	assert( eb_services[account->service_id].protocol_id == SERVICE_INFO.protocol_id );

	return alad->status;
}

static void eb_aim_set_current_state( eb_local_account * account, int state )
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;

	/* stop the recursion */
	if( is_setting_state )
		return;

	eb_debug(DBG_TOC, "eb_set_current_state %d\n", state );
//	assert( eb_services[account->service_id].protocol_id == SERVICE_INFO.protocol_id );
	if(account == NULL || account->protocol_local_account_data == NULL )
	{
		g_warning("ACCOUNT state == NULL!!!!!!!!!!!!!!!!!!!!!");
	}

	switch(state) {
	case AIM_ONLINE:
		if (account->connected == 0 && account->connecting == 0) {
			eb_aim_login(account);
		}
		toc_set_away(alad->conn, NULL);
		break;
	case AIM_AWAY:
		if (account->connected == 0 && account->connecting == 0) {
			eb_aim_login(account);
		}
		if (is_away) {
			char *awaymsg = get_away_message();
			toc_set_away(alad->conn, awaymsg);
			g_free(awaymsg);
		} else
			toc_set_away(alad->conn, _("User is currently away"));
		break;
	case AIM_OFFLINE:
		if (account->connected == 1) {
			eb_aim_logout(account);
		}
		break;
	}
	alad->status = state;

}

static void eb_aim_set_away(eb_local_account * account, char * message)
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)account->protocol_local_account_data;

	if (message) {
		if(account->status_menu)
			eb_set_active_menu_status(account->status_menu, AIM_AWAY);
		toc_set_away(alad->conn, message);
	} else {
		if(account->status_menu)
			eb_set_active_menu_status(account->status_menu, AIM_ONLINE);
	}
}

static char **eb_aim_get_status_pixmap( eb_account * account)
{
	struct eb_aim_account_data * aad;
	
	aad = account->protocol_account_data;

	if (aad->status == AIM_ONLINE)
		return aim_online_xpm;
	else
		return aim_away_xpm;
	
}

static char * eb_aim_get_status_string( eb_account * account )
{
	static char string[255], buf[255];
	struct eb_aim_account_data * aad = account->protocol_account_data;
	strcpy(buf, "");
	strcpy(string, "");
	if(aad->idle_time)
	{
		int hours, minutes, days;
		minutes = (time(NULL) - aad->idle_time)/60;
		hours = minutes/60;
		minutes = minutes%60;
		days = hours/24;
		hours = hours%24;
		if( days )
		{
			g_snprintf( buf, 255, " %d:%02d:%02d", days, hours, minutes );
		}
		else if(hours)
		{
			g_snprintf( buf, 255, " %d:%02d", hours, minutes);
		}
		else
		{
			g_snprintf( buf, 255, " %d", minutes); 
		}
	}

	if (aad->evil)
		g_snprintf(string, 255, "[%d%%]%s", aad->evil, buf);
	else
		g_snprintf(string, 255, "%s", buf);

	if (!account->online)
		g_snprintf(string, 255, "Offline");		

	return string;
}

static void eb_aim_set_idle( eb_local_account * ela, int idle )
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)ela->protocol_local_account_data;
	eb_debug(DBG_TOC, "eb_aim_set_idle %d %d\n", alad->conn->fd, alad->conn->seq_num );
	toc_set_idle( alad->conn, idle );
}

static void eb_aim_real_change_group(eb_account * ea, const char *old_group, const char *new_group)
{
	char str[2048];
	struct eb_aim_local_account_data * alad;
	LList * node;
	
	g_snprintf(str, 2048, "g %s\nb %s", new_group, ea->handle);

	if( eb_services[ea->service_id].protocol_id != SERVICE_INFO.protocol_id )
		return;

	for( node = accounts; node; node=node->next )
	{
		eb_local_account * ela = node->data;
		if( ela && ela->connected && ela->service_id == ea->service_id)
		{
			alad = ela->protocol_local_account_data;
			toc_remove_buddy(alad->conn, ea->handle, (char *)old_group);
			toc_add_buddy(alad->conn, ea->handle, (char *)new_group);
		}
	}
}

static void eb_aim_change_group(eb_account * ea, const char *new_group)
{
	eb_aim_real_change_group(ea, ea->account_contact->group->name, new_group);
}

static void eb_aim_del_group(const char *group)
{
	struct eb_aim_local_account_data * alad;
	LList * node;
	
	for( node = accounts; node; node=node->next )
	{
		eb_local_account * ela = node->data;
		if( ela && ela->connected && ela->service_id == SERVICE_INFO.protocol_id)
		{
			alad = ela->protocol_local_account_data;
			toc_remove_group(alad->conn, group);
		}
	}
}
	
static void eb_aim_add_group(const char *group)
{
	struct eb_aim_local_account_data * alad;
	LList * node;
	
	for( node = accounts; node; node=node->next )
	{
		eb_local_account * ela = node->data;
		if( ela && ela->connected && ela->service_id == SERVICE_INFO.protocol_id)
		{
			alad = ela->protocol_local_account_data;
			toc_add_group(alad->conn, group);
		}
	}	
}

static void eb_aim_rename_group(const char *old_group, const char *new_group)
{
	LList *l;
	
	for(l = aim_buddies; l; l=l->next)
	{
		eb_account *ea = find_account_by_handle(l->data, SERVICE_INFO.protocol_id);
		if (ea) 
			eb_debug(DBG_TOC, "checking if we should move %s from %s\n",ea->handle, ea->account_contact->group->name);
		if (ea && !strcmp(ea->account_contact->group->name, new_group)) {
			eb_debug(DBG_TOC, "Moving %s from %s to %s\n",ea->handle, old_group, new_group);
			eb_aim_real_change_group(ea, old_group, new_group);
		}
	}
}

static void eb_aim_get_info( eb_local_account * from, eb_account * account_to )
{
	struct eb_aim_local_account_data * alad;
	alad = (struct eb_aim_local_account_data *)from->protocol_local_account_data;

	toc_get_info( alad->conn, account_to->handle );
}

static void aim_info_update(eb_account *sender)
{
	info_window *iw = sender->infowindow;
	char * data = (char *)iw->info_data;
	eb_info_window_clear(iw);
	eb_info_window_add_info(sender, data,1,1,0);
}

static void aim_info_data_cleanup(info_window * iw)
{
}

/*	There are no prefs for AIM-TOC at the moment.

static input_list * eb_aim_get_prefs()
{
	return aim_prefs;
}
*/

static void eb_aim_read_prefs_config(LList * values)
{
	char * c;
	c = value_pair_get_value(values, "server");
	if(c)
	{
		strncpy(aim_server, c, sizeof(aim_server));
	}
	c = value_pair_get_value(values, "port");
	if(c)
	{
		strncpy(aim_port, c, sizeof(aim_port));
	}
	c = value_pair_get_value(values, "do_aim_debug");
	if(c)
	{
		do_aim_debug = atoi(c);
	}
}

static LList * eb_aim_write_prefs_config()
{
	LList * config = NULL;
	char buffer[5];

	config = value_pair_add(config, "server", aim_server);
	config = value_pair_add(config, "port", aim_port);
	snprintf(buffer, 5, "%d", do_aim_debug);
	config = value_pair_add(config, "do_aim_debug", buffer);

	return config;
}


struct service_callbacks * query_callbacks()
{
	struct service_callbacks * sc;
	
	toc_im_in = eb_aim_parse_incoming_im;
	update_user_status = eb_aim_oncoming_buddy; 
	toc_error_message = eb_aim_error_message;
	toc_disconnect = eb_aim_disconnect;
	toc_chat_im_in = eb_aim_toc_chat_im_in;
	toc_chat_invite = eb_aim_chat_invite;
	toc_join_ack = eb_aim_join_ack;
	toc_chat_update_buddy = eb_aim_chat_update_buddy;
	toc_begin_file_recieve = progress_window_new;
	toc_update_file_status = update_progress;
	toc_complete_file_recieve = progress_window_close;
	toc_file_offer = eb_aim_file_offer;
	toc_user_info = eb_aim_user_info;
	toc_new_user = eb_aim_new_user;
	toc_new_group = eb_aim_new_group;
	toc_logged_in = eb_aim_logged_in;
	
	sc = g_new0( struct service_callbacks, 1 );
	sc->query_connected = eb_aim_query_connected;
	sc->login = eb_aim_login;
	sc->logout = eb_aim_logout;
	sc->send_im = eb_aim_send_im;
	sc->read_local_account_config = eb_aim_read_local_config;
	sc->write_local_config = eb_aim_write_local_config;
	sc->read_account_config = eb_aim_read_config;
	sc->get_states = eb_aim_get_states;
	sc->get_current_state = eb_aim_get_current_state;
	sc->set_current_state = eb_aim_set_current_state;
	sc->check_login = eb_aim_check_login;
	sc->add_user = eb_aim_add_user;
	sc->del_user = eb_aim_del_user;
	sc->new_account = eb_aim_new_account;
	sc->get_status_string = eb_aim_get_status_string;
	sc->get_status_pixmap = eb_aim_get_status_pixmap;
	sc->set_idle = eb_aim_set_idle;
	sc->set_away = eb_aim_set_away;
	sc->send_chat_room_message = eb_aim_send_chat_room_message;
	sc->join_chat_room = eb_aim_join_chat_room;
	sc->leave_chat_room = eb_aim_leave_chat_room;
	sc->make_chat_room = eb_aim_make_chat_room;
	sc->send_invite = eb_aim_send_invite;
	sc->accept_invite = eb_aim_accept_invite;
	sc->decline_invite = eb_aim_decline_invite;
	sc->get_info = eb_aim_get_info;

	sc->get_prefs = NULL;
	sc->read_prefs_config = eb_aim_read_prefs_config;
	sc->write_prefs_config = eb_aim_write_prefs_config;

	sc->get_color = eb_toc_get_color;
	sc->get_smileys = eb_default_smileys;
	
	sc->change_group = eb_aim_change_group;
	sc->add_group = eb_aim_add_group;
	sc->del_group = eb_aim_del_group;
	sc->rename_group = eb_aim_rename_group;
	return sc;
}
