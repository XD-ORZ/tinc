/*
    control.c -- Control socket handling.
    Copyright (C) 2007 Guus Sliepen <guus@tinc-vpn.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "system.h"
#include "crypto.h"
#include "conf.h"
#include "control.h"
#include "control_common.h"
#include "graph.h"
#include "logger.h"
#include "meta.h"
#include "net.h"
#include "netutl.h"
#include "protocol.h"
#include "route.h"
#include "splay_tree.h"
#include "utils.h"
#include "xalloc.h"

char controlcookie[65];
extern char *pidfilename;

static bool control_return(connection_t *c, int type, int error) {
	return send_request(c, "%d %d %d", CONTROL, type, error);
}

static bool control_ok(connection_t *c, int type) {
	return control_return(c, type, 0);
}

bool control_h(connection_t *c, char *request) {
	int type;

	if(!c->status.control || c->allow_request != CONTROL) {
		logger(LOG_ERR, "Unauthorized control request from %s (%s)", c->name, c->hostname);
		return false;
	}

	if(sscanf(request, "%*d %d", &type) != 1) {
		logger(LOG_ERR, "Got bad %s from %s (%s)", "CONTROL", c->name, c->hostname);
		return false;
	}

	switch (type) {
		case REQ_STOP:
			event_loopexit(NULL);
			return control_ok(c, REQ_STOP);

		case REQ_DUMP_NODES:
			return dump_nodes(c);
			
		case REQ_DUMP_EDGES:
			return dump_edges(c);

		case REQ_DUMP_SUBNETS:
			return dump_subnets(c);

		case REQ_DUMP_CONNECTIONS:
			return dump_connections(c);

		case REQ_PURGE:
			purge();
			return control_ok(c, REQ_PURGE);

		case REQ_SET_DEBUG: {
			int new_level;
			if(sscanf(request, "%*d %*d %d", &new_level) != 1)
				return false;
			send_request(c, "%d %d %d", CONTROL, REQ_SET_DEBUG, debug_level);
			if(new_level >= 0)
				debug_level = new_level;
			return true;
		}

		case REQ_RETRY:
			retry();
			return control_ok(c, REQ_RETRY);

		case REQ_RELOAD:
			logger(LOG_NOTICE, "Got '%s' command", "reload");
			int result = reload_configuration();
			return control_return(c, REQ_RELOAD, result);

		case REQ_DISCONNECT: {
			char name[MAX_STRING_SIZE];
			connection_t *other;
			splay_node_t *node, *next;
			bool found = false;

			if(sscanf(request, "%*d %*d " MAX_STRING, name) != 1)
				return control_return(c, REQ_DISCONNECT, -1);

			for(node = connection_tree->head; node; node = next) {
				next = node->next;
				other = node->data;
				if(strcmp(other->name, name))
					continue;
				terminate_connection(other, other->status.active);
				found = true;
			}

			return control_return(c, REQ_DISCONNECT, found ? 0 : -2);
		}

		case REQ_DUMP_TRAFFIC:
			return dump_traffic(c);

		case REQ_PCAP:
			c->status.pcap = true;
			pcap = true;
			return true;

		default:
			return send_request(c, "%d %d", CONTROL, REQ_INVALID);
	}
}

bool init_control(void) {
	randomize(controlcookie, sizeof controlcookie / 2);
	bin2hex(controlcookie, controlcookie, sizeof controlcookie / 2);
	controlcookie[sizeof controlcookie - 1] = 0;

	FILE *f = fopen(pidfilename, "w");
	if(!f) {
		logger(LOG_ERR, "Cannot write control socket cookie file %s: %s", pidfilename, strerror(errno));
		return false;
	}

#ifdef HAVE_FCHMOD
	fchmod(fileno(f), 0600);
#else
	chmod(pidfilename, 0600);
#endif
	// Get the address and port of the first listening socket

	char *localhost = NULL;
	sockaddr_t sa;
	socklen_t len = sizeof sa;

	if(getsockname(listen_socket[0].tcp, (struct sockaddr *)&sa, &len))
		xasprintf(&localhost, "127.0.0.1 port %d", myport);
	else
		localhost = sockaddr2hostname(&sa);

	fprintf(f, "%d %s %s\n", (int)getpid(), controlcookie, localhost);

	free(localhost);
	fclose(f);

	return true;
}

void exit_control(void) {
	unlink(pidfilename);
}