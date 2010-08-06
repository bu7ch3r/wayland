/*
 * Copyright © 2010 Intel Corporation
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <expat.h>

#include "wayland-util.h"

static const char copyright[] =
	"/*\n"
	" * Copyright © 2010 Kristian Høgsberg\n"
	" *\n"
	" * Permission to use, copy, modify, distribute, and sell this software and its\n"
	" * documentation for any purpose is hereby granted without fee, provided that\n"
	" * the above copyright notice appear in all copies and that both that copyright\n"
	" * notice and this permission notice appear in supporting documentation, and\n"
	" * that the name of the copyright holders not be used in advertising or\n"
	" * publicity pertaining to distribution of the software without specific,\n"
	" * written prior permission.  The copyright holders make no representations\n"
	" * about the suitability of this software for any purpose.  It is provided \"as\n"
	" * is\" without express or implied warranty.\n"
	" *\n"
	" * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,\n"
	" * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO\n"
	" * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR\n"
	" * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,\n"
	" * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER\n"
	" * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE\n"
	" * OF THIS SOFTWARE.\n"
	" */\n";

static int
usage(int ret)
{
	fprintf(stderr, "usage: ./scanner [header|code]\n");
	exit(ret);
}

#define XML_BUFFER_SIZE 4096

struct protocol {
	struct wl_list interface_list;
};

struct interface {
	char *name;
	char *uppercase_name;
	int version;
	struct wl_list request_list;
	struct wl_list event_list;
	struct wl_list link;
};

struct message {
	char *name;
	char *uppercase_name;
	struct wl_list arg_list;
	struct wl_list link;
};

enum arg_type {
	NEW_ID,
	INT,
	UNSIGNED,
	STRING,
	OBJECT,
	ARRAY
};

struct arg {
	char *name;
	enum arg_type type;
	char *object_name;
	struct wl_list link;
};

struct parse_context {
	struct protocol *protocol;
	struct interface *interface;
	struct message *message;
};

static char *
uppercase_dup(const char *src)
{
	char *u;
	int i;

	u = strdup(src);
	for (i = 0; u[i]; i++)
		u[i] = toupper(u[i]);
	u[i] = '\0';

	return u;
}

static void
start_element(void *data, const char *element_name, const char **atts)
{
	struct parse_context *ctx = data;
	struct interface *interface;
	struct message *message;
	struct arg *arg;
	const char *name, *type;
	int i, version;

	name = 0;
	type = 0;
	version = 0;
	for (i = 0; atts[i]; i += 2) {
		if (strcmp(atts[i], "name") == 0)
			name = atts[i + 1];
		if (strcmp(atts[i], "version") == 0)
			version = atoi(atts[i + 1]);
		if (strcmp(atts[i], "type") == 0)
			type = atts[i + 1];
	}

	if (strcmp(element_name, "interface") == 0) {
		if (name == NULL) {
			fprintf(stderr, "no interface name given\n");
			exit(EXIT_FAILURE);
		}

		if (version == 0) {
			fprintf(stderr, "no interface version given\n");
			exit(EXIT_FAILURE);
		}

		interface = malloc(sizeof *interface);
		interface->name = strdup(name);
		interface->uppercase_name = uppercase_dup(name);
		interface->version = version;
		wl_list_init(&interface->request_list);
		wl_list_init(&interface->event_list);
		wl_list_insert(ctx->protocol->interface_list.prev,
			       &interface->link);
		ctx->interface = interface;
	} else if (strcmp(element_name, "request") == 0 ||
		   strcmp(element_name, "event") == 0) {
		if (name == NULL) {
			fprintf(stderr, "no request name given\n");
			exit(EXIT_FAILURE);
		}

		message = malloc(sizeof *message);
		message->name = strdup(name);
		message->uppercase_name = uppercase_dup(name);
		wl_list_init(&message->arg_list);

		if (strcmp(element_name, "request") == 0)
			wl_list_insert(ctx->interface->request_list.prev,
				       &message->link);
		else
			wl_list_insert(ctx->interface->event_list.prev,
				       &message->link);

		ctx->message = message;
	} else if (strcmp(element_name, "arg") == 0) {
		arg = malloc(sizeof *arg);
		arg->name = strdup(name);

		if (strcmp(type, "new_id") == 0)
			arg->type = NEW_ID;
		else if (strcmp(type, "int") == 0)
			arg->type = INT;
		else if (strcmp(type, "uint") == 0)
			arg->type = UNSIGNED;
		else if (strcmp(type, "string") == 0)
			arg->type = STRING;
		else if (strcmp(type, "array") == 0)
			arg->type = ARRAY;
		else {
			arg->type = OBJECT;
			arg->object_name = strdup(type);
		}

		wl_list_insert(ctx->message->arg_list.prev,
			       &arg->link);
	}
}

static void
emit_opcodes(struct wl_list *message_list, struct interface *interface)
{
	struct message *m;
	int opcode;

	if (wl_list_empty(message_list))
		return;

	opcode = 0;
	wl_list_for_each(m, message_list, link)
		printf("#define WL_%s_%s\t%d\n",
		       interface->uppercase_name, m->uppercase_name, opcode++);

	printf("\n");
}

static void
emit_structs(struct wl_list *message_list, struct interface *interface)
{
	struct message *m;
	struct arg *a;
	int is_interface;

	is_interface = message_list == &interface->request_list;
	printf("struct wl_%s_%s {\n", interface->name,
	       is_interface ? "interface" : "listener");

	wl_list_for_each(m, message_list, link) {
		printf("\tvoid (*%s)(", m->name);

		if (is_interface) {
			printf("struct wl_client *client, struct wl_%s *%s",
			       interface->name, interface->name);
		} else {
			printf("void *data, struct wl_%s *%s",
			       interface->name, interface->name);
		}

		if (!wl_list_empty(&m->arg_list))
			printf(", ");

		wl_list_for_each(a, &m->arg_list, link) {
			switch (a->type) {
			default:
			case INT:
				printf("int32_t ");
				break;
			case NEW_ID:
			case UNSIGNED:
				printf("uint32_t ");
				break;
			case STRING:
				printf("const char *");
				break;
			case OBJECT:
				printf("struct wl_%s *", a->object_name);
				break;
			case ARRAY:
				printf("struct wl_array *");
				break;
			}
			printf("%s%s",
			       a->name,
			       a->link.next == &m->arg_list ? "" : ", ");
		}

		printf(");\n");
	}

	printf("};\n\n");
}

static void
emit_header(struct protocol *protocol, int server)
{
	struct interface *i;

	printf("%s\n\n"
	       "#ifndef WAYLAND_PROTOCOL_H\n"
	       "#define WAYLAND_PROTOCOL_H\n"
	       "\n"
	       "#ifdef  __cplusplus\n"
	       "extern \"C\" {\n"
	       "#endif\n"
	       "\n"
	       "#include <stdint.h>\n"
	       "#include \"wayland-util.h\"\n\n"
	       "struct wl_client;\n\n", copyright);

	wl_list_for_each(i, &protocol->interface_list, link)
		printf("struct wl_%s;\n", i->name);
	printf("\n");

	wl_list_for_each(i, &protocol->interface_list, link) {

		if (server) {
			emit_structs(&i->request_list, i);
			emit_opcodes(&i->event_list, i);
		} else {
			emit_structs(&i->event_list, i);
			emit_opcodes(&i->request_list, i);
		}

		printf("extern const struct wl_interface "
		       "wl_%s_interface;\n\n",
		       i->name);
	}

	printf("#ifdef  __cplusplus\n"
	       "}\n"
	       "#endif\n"
	       "\n"
	       "#endif\n");
}

static void
emit_messages(struct wl_list *message_list,
	      struct interface *interface, const char *suffix)
{
	struct message *m;
	struct arg *a;

	if (wl_list_empty(message_list))
		return;

	printf("static const struct wl_message "
	       "%s_%s[] = {\n",
	       interface->name, suffix);

	wl_list_for_each(m, message_list, link) {
		printf("\t{ \"%s\", \"", m->name);
		wl_list_for_each(a, &m->arg_list, link) {
			switch (a->type) {
			default:
			case INT:
				printf("i");
				break;
			case NEW_ID:
				printf("n");
				break;
			case UNSIGNED:
				printf("u");
				break;
			case STRING:
				printf("s");
				break;
			case OBJECT:
				printf("o");
				break;
			case ARRAY:
				printf("a");
				break;
			}
		}
		printf("\" },\n");
	}

	printf("};\n\n");
}

static void
emit_code(struct protocol *protocol)
{
	struct interface *i;

	printf("%s\n\n"
	       "#include <stdlib.h>\n"
	       "#include <stdint.h>\n"
	       "#include \"wayland-util.h\"\n\n",
	       copyright);

	wl_list_for_each(i, &protocol->interface_list, link) {

		emit_messages(&i->request_list, i, "requests");
		emit_messages(&i->event_list, i, "events");

		printf("WL_EXPORT const struct wl_interface "
		       "wl_%s_interface = {\n"
		       "\t\"%s\", %d,\n",
		       i->name, i->name, i->version);

		if (!wl_list_empty(&i->request_list))
			printf("\tARRAY_LENGTH(%s_requests), %s_requests,\n",
			       i->name, i->name);
		else
			printf("\t0, NULL,\n");

		if (!wl_list_empty(&i->event_list))
			printf("\tARRAY_LENGTH(%s_events), %s_events,\n",
			       i->name, i->name);
		else
			printf("\t0, NULL,\n");

		printf("};\n\n");
	}
}

int main(int argc, char *argv[])
{
	struct parse_context ctx;
	struct protocol protocol;
	XML_Parser parser;
	int len;
	void *buf;

	if (argc != 2)
		usage(EXIT_FAILURE);

	wl_list_init(&protocol.interface_list);
	ctx.protocol = &protocol;

	parser = XML_ParserCreate(NULL);
	XML_SetUserData(parser, &ctx);
	if (parser == NULL) {
		fprintf(stderr, "failed to create parser\n");
		exit(EXIT_FAILURE);
	}

	XML_SetElementHandler(parser, start_element, NULL);
	do {
		buf = XML_GetBuffer(parser, XML_BUFFER_SIZE);
		len = fread(buf, 1, XML_BUFFER_SIZE, stdin);
		if (len < 0) {
			fprintf(stderr, "fread: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		XML_ParseBuffer(parser, len, len == 0);

	} while (len > 0);

	XML_ParserFree(parser);

	if (strcmp(argv[1], "client-header") == 0) {
		emit_header(&protocol, 0);
	} else if (strcmp(argv[1], "server-header") == 0) {
		emit_header(&protocol, 1);
	} else if (strcmp(argv[1], "code") == 0) {
		emit_code(&protocol);
	}

	return 0;
}