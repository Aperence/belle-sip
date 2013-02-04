/*
	belle-sip - SIP (RFC3261) library.
    Copyright (C) 2010  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "belle_sip_resolver.h"

#include <stdlib.h>


#define DNS_EAGAIN  EAGAIN


static struct dns_resolv_conf *resconf(belle_sip_resolver_context_t *ctx) {
#ifndef _WIN32
	const char *path;
#endif
	int error;

	if (ctx->resconf)
		return ctx->resconf;

	if (!(ctx->resconf = dns_resconf_open(&error))) {
		belle_sip_error("%s dns_resconf_open error: %s", __FUNCTION__, dns_strerror(error));
		return NULL;
	}

#ifdef _WIN32
	error = dns_resconf_loadwin(ctx->resconf);
#else
	path = "/etc/resolv.conf";
	error = dns_resconf_loadpath(ctx->resconf, path);
	if (error) {
		belle_sip_error("%s dns_resconf_loadpath error [%s]: %s", __FUNCTION__, path, dns_strerror(error));
		return NULL;
	}

	path = "/etc/nsswitch.conf";
	error = dns_nssconf_loadpath(ctx->resconf, path);
	if (error) {
		belle_sip_error("%s dns_nssconf_loadpath error [%s]: %s", __FUNCTION__, path, dns_strerror(error));
		return NULL;
	}
#endif

	return ctx->resconf;
}

static struct dns_hosts *hosts(belle_sip_resolver_context_t *ctx) {
	int error;

	if (ctx->hosts)
		return ctx->hosts;

	if (!(ctx->hosts = dns_hosts_local(&error))) {
		belle_sip_error("%s dns_hosts_local error: %s", __FUNCTION__, dns_strerror(error));
		return NULL;
	}

	return ctx->hosts;
}

struct dns_cache *cache(belle_sip_resolver_context_t *ctx) {
	return NULL;
}

static int resolver_process_a_data(belle_sip_resolver_context_t *ctx, unsigned int revents) {
	char host[NI_MAXHOST + 1];
	char service[NI_MAXSERV + 1];
	struct dns_packet *ans;
	struct dns_rr_i *I;
	int error;
#ifndef HAVE_C99
	struct dns_rr_i dns_rr_it;
#endif

	if (revents & BELLE_SIP_EVENT_TIMEOUT) {
		belle_sip_error("%s timed-out", __FUNCTION__);
		return BELLE_SIP_STOP;
	}
	if (ctx->cancelled) {
		return BELLE_SIP_STOP;
	}

	error = dns_res_check(ctx->R);
	if (!error) {
		struct dns_rr rr;
		union dns_any any;
		enum dns_section section = DNS_S_AN;

		ans = dns_res_fetch(ctx->R, &error);
#ifdef HAVE_C99
		I = dns_rr_i_new(ans, .section = 0);
#else
		memset(&dns_rr_it, 0, sizeof dns_rr_it);
		I = dns_rr_i_init(&dns_rr_it, ans);
#endif
		while (dns_rr_grep(&rr, 1, I, ans, &error)) {
			if (rr.section == section) {
				if ((error = dns_any_parse(dns_any_init(&any, sizeof(any)), &rr, ans))) {
					belle_sip_error("%s dns_any_parse error: %s", __FUNCTION__, dns_strerror(error));
					free(ans);
					return BELLE_SIP_STOP;
				}
				if ((ctx->family == AF_INET6) && (rr.class == DNS_C_IN) && (rr.type == DNS_T_AAAA)) {
					struct dns_aaaa *aaaa = &any.aaaa;
					struct sockaddr_in6 sin6;
					memset(&sin6, 0, sizeof(sin6));
					memcpy(&sin6.sin6_addr, &aaaa->addr, sizeof(sin6.sin6_addr));
					sin6.sin6_family = AF_INET6;
					sin6.sin6_port = ctx->port;
					if (getnameinfo((struct sockaddr *)&sin6, sizeof(sin6), host, sizeof(host), service, sizeof(service), NI_NUMERICHOST) != 0)
						continue;
					ctx->ai = belle_sip_ip_address_to_addrinfo(host, ctx->port);
					belle_sip_message("%s has address %s", ctx->name, host);
					break;
				} else {
					if ((rr.class == DNS_C_IN) && (rr.type == DNS_T_A)) {
						struct dns_a *a = &any.a;
						struct sockaddr_in sin;
						memset(&sin, 0, sizeof(sin));
						memcpy(&sin.sin_addr, &a->addr, sizeof(sin.sin_addr));
						sin.sin_family = AF_INET;
						sin.sin_port = ctx->port;
						if (getnameinfo((struct sockaddr *)&sin, sizeof(sin), host, sizeof(host), service, sizeof(service), NI_NUMERICHOST) != 0)
							continue;
						ctx->ai = belle_sip_ip_address_to_addrinfo(host, ctx->port);
						belle_sip_message("%s has address %s", ctx->name, host);
						break;
					}
				}
			}
		}
		free(ans);
		ctx->cb(ctx->cb_data, ctx->name, ctx->ai);
		return BELLE_SIP_STOP;
	}
	if (error != DNS_EAGAIN) {
		belle_sip_error("%s dns_res_check error: %s (%d)", __FUNCTION__, dns_strerror(error), error);
		return BELLE_SIP_STOP;
	}

	dns_res_poll(ctx->R, 0);
	return BELLE_SIP_CONTINUE;
}

static int resolver_start_query(belle_sip_resolver_context_t *ctx, belle_sip_source_func_t datafunc, enum dns_type type, int timeout) {
	struct dns_hints *(*hints)() = &dns_hints_local;
	struct dns_options *opts;
#ifndef HAVE_C99
	struct dns_options opts_st;
#endif
	int error;

	if (!ctx->name) return -1;

	if (resconf(ctx))
		resconf(ctx)->options.recurse = 0;
	else
		return -1;
	if (!hosts(ctx))
		return -1;

#ifdef HAVE_C99
	opts = dns_opts();
#else
	memset(&opts_st, 0, sizeof opts_st);
	opts = &opts_st;
#endif
	if (!(ctx->R = dns_res_open(ctx->resconf, ctx->hosts, dns_hints_mortal(hints(ctx->resconf, &error)), cache(ctx), opts, &error))) {
		belle_sip_error("%s dns_res_open error [%s]: %s", __FUNCTION__, ctx->name, dns_strerror(error));
		return -1;
	}

	if ((error = dns_res_submit(ctx->R, ctx->name, type, DNS_C_IN))) {
		belle_sip_error("%s dns_res_submit error [%s]: %s", __FUNCTION__, ctx->name, dns_strerror(error));
		return -1;
	}

	(*datafunc)(ctx, 0);
	belle_sip_socket_source_init((belle_sip_source_t*)ctx, datafunc, ctx, dns_res_pollfd(ctx->R), BELLE_SIP_EVENT_READ | BELLE_SIP_EVENT_TIMEOUT, timeout);
	return 0;
}



int belle_sip_addrinfo_to_ip(const struct addrinfo *ai, char *ip, size_t ip_size, int *port){
	char serv[16];
	int err=getnameinfo(ai->ai_addr,ai->ai_addrlen,ip,ip_size,serv,sizeof(serv),NI_NUMERICHOST|NI_NUMERICSERV);
	if (err!=0){
		belle_sip_error("getnameinfo() error: %s",gai_strerror(err));
		strncpy(ip,"<bug!!>",ip_size);
	}
	if (port) *port=atoi(serv);
	return 0;
}

struct addrinfo * belle_sip_ip_address_to_addrinfo(const char *ipaddress, int port){
	struct addrinfo *res=NULL;
	struct addrinfo hints={0};
	char serv[10];
	int err;

	snprintf(serv,sizeof(serv),"%i",port);
	hints.ai_family=AF_UNSPEC;
	hints.ai_flags=AI_NUMERICSERV|AI_NUMERICHOST;
	err=getaddrinfo(ipaddress,serv,&hints,&res);
	if (err!=0){
		return NULL;
	}
	return res;
}


static void belle_sip_resolver_context_destroy(belle_sip_resolver_context_t *ctx){
	if (ctx->name)
		belle_sip_free(ctx->name);
	if (ctx->ai)
		freeaddrinfo(ctx->ai);
	if (ctx->R)
		dns_res_close(ctx->R);
	if (ctx->hosts)
		free(ctx->hosts);
	if (ctx->resconf)
		free(ctx->resconf);
}

BELLE_SIP_DECLARE_NO_IMPLEMENTED_INTERFACES(belle_sip_resolver_context_t);
BELLE_SIP_INSTANCIATE_VPTR(belle_sip_resolver_context_t, belle_sip_source_t,belle_sip_resolver_context_destroy, NULL, NULL,FALSE);

unsigned long belle_sip_resolve(belle_sip_stack_t *stack, const char *name, int port, int family, belle_sip_resolver_callback_t cb , void *data, belle_sip_main_loop_t *ml) {
	struct addrinfo *res = belle_sip_ip_address_to_addrinfo(name, port);
	if (res == NULL) {
		/* Then perform asynchronous DNS query */
		belle_sip_resolver_context_t *ctx = belle_sip_object_new(belle_sip_resolver_context_t);
		ctx->cb_data = data;
		ctx->cb = cb;
		ctx->name = belle_sip_strdup(name);
		ctx->port = port;
		ctx->ai = NULL;
		if (family == 0) family = AF_UNSPEC;
		ctx->family = family;
		if (resolver_start_query(ctx,
				(belle_sip_source_func_t)resolver_process_a_data,
				(ctx->family == AF_INET6) ? DNS_T_AAAA : DNS_T_A,
				belle_sip_stack_get_dns_timeout(stack)) < 0) {
			belle_sip_object_unref(ctx);
			return 0;
		}

		/* The resolver context must never be removed manually from the main loop */
		belle_sip_main_loop_add_source(ml,(belle_sip_source_t*)ctx);
		belle_sip_object_unref(ctx);	/* The main loop has a ref on it */
		return ctx->source.id;
	} else {
		cb(data, name, res);
		return 0;
	}
}

void belle_sip_resolve_cancel(belle_sip_main_loop_t *ml, unsigned long id){
	if (id!=0){
		belle_sip_source_t *s=belle_sip_main_loop_find_source(ml,id);
		if (s){
			belle_sip_resolver_context_t *res=BELLE_SIP_RESOLVER_CONTEXT(s);
			res->cancelled=1;
		}
	}
}

void belle_sip_get_src_addr_for(const struct sockaddr *dest, socklen_t destlen, struct sockaddr *src, socklen_t *srclen){
	int af_type=dest->sa_family;
	int sock=socket(af_type,SOCK_DGRAM,IPPROTO_UDP);
	
	if (sock==(belle_sip_socket_t)-1){
		belle_sip_fatal("Could not create socket: %s",belle_sip_get_socket_error_string());
		goto fail;
	}
	if (connect(sock,dest,destlen)==-1){
		belle_sip_error("belle_sip_get_src_addr_for: connect() failed: %s",belle_sip_get_socket_error_string());
		goto fail;
	}
	if (getsockname(sock,src,srclen)==-1){
		belle_sip_error("belle_sip_get_src_addr_for: getsockname() failed: %s",belle_sip_get_socket_error_string());
		goto fail;
	}
	close_socket(sock);
	return;
fail:
	{
		struct addrinfo hints={0},*res=NULL;
		int err;
		hints.ai_family=af_type;
		err=getaddrinfo(af_type==AF_INET ? "0.0.0.0" : "::0","0",&hints,&res);
		if (err!=0) belle_sip_fatal("belle_sip_get_src_addr_for(): getaddrinfo failed: %s",belle_sip_get_socket_error_string_from_code(err));
		memcpy(src,res->ai_addr,MIN(*srclen,res->ai_addrlen));
		*srclen=res->ai_addrlen;
		freeaddrinfo(res);
	}
	if (sock==(belle_sip_socket_t)-1) close_socket(sock);
}
