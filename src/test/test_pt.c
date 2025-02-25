/* Copyright (c) 2001-2004, Roger Dingledine.
 * Copyright (c) 2004-2006, Roger Dingledine, Nick Mathewson.
 * Copyright (c) 2007-2019, The Tor Project, Inc. */
/* See LICENSE for licensing information */

#include "orconfig.h"
#define PT_PRIVATE
#define UTIL_PRIVATE
#define STATEFILE_PRIVATE
#define CONTROL_EVENTS_PRIVATE
#define PROCESS_PRIVATE
#include "core/or/or.h"
#include "app/config/config.h"
#include "app/config/confparse.h"
#include "feature/control/control.h"
#include "feature/control/control_events.h"
#include "feature/client/transports.h"
#include "core/or/circuitbuild.h"
#include "app/config/statefile.h"
#include "test/test.h"
#include "lib/encoding/confline.h"
#include "lib/net/resolve.h"
#include "lib/process/process.h"

#include "app/config/or_state_st.h"

#include "test/log_test_helpers.h"

static void
reset_mp(managed_proxy_t *mp)
{
  mp->conf_state = PT_PROTO_LAUNCHED;
  SMARTLIST_FOREACH(mp->transports, transport_t *, t, transport_free(t));
  smartlist_clear(mp->transports);
}

static void
test_pt_parsing(void *arg)
{
  char line[200];
  transport_t *transport = NULL;
  tor_addr_t test_addr;

  managed_proxy_t *mp = tor_malloc_zero(sizeof(managed_proxy_t));
  (void)arg;
  mp->conf_state = PT_PROTO_INFANT;
  mp->transports = smartlist_new();

  /* incomplete cmethod */
  strlcpy(line,"CMETHOD trebuchet",sizeof(line));
  tt_int_op(parse_cmethod_line(line, mp), OP_LT, 0);

  reset_mp(mp);

  /* wrong proxy type */
  strlcpy(line,"CMETHOD trebuchet dog 127.0.0.1:1999",sizeof(line));
  tt_int_op(parse_cmethod_line(line, mp), OP_LT, 0);

  reset_mp(mp);

  /* wrong addrport */
  strlcpy(line,"CMETHOD trebuchet socks4 abcd",sizeof(line));
  tt_int_op(parse_cmethod_line(line, mp), OP_LT, 0);

  reset_mp(mp);

  /* correct line */
  strlcpy(line,"CMETHOD trebuchet socks5 127.0.0.1:1999",sizeof(line));
  tt_int_op(parse_cmethod_line(line, mp), OP_EQ, 0);
  tt_int_op(smartlist_len(mp->transports), OP_EQ, 1);
  transport = smartlist_get(mp->transports, 0);
  /* test registered address of transport */
  tor_addr_parse(&test_addr, "127.0.0.1");
  tt_assert(tor_addr_eq(&test_addr, &transport->addr));
  /* test registered port of transport */
  tt_uint_op(transport->port, OP_EQ, 1999);
  /* test registered SOCKS version of transport */
  tt_int_op(transport->socks_version, OP_EQ, PROXY_SOCKS5);
  /* test registered name of transport */
  tt_str_op(transport->name,OP_EQ, "trebuchet");

  reset_mp(mp);

  /* incomplete smethod */
  strlcpy(line,"SMETHOD trebuchet",sizeof(line));
  tt_int_op(parse_smethod_line(line, mp), OP_LT, 0);

  reset_mp(mp);

  /* wrong addr type */
  strlcpy(line,"SMETHOD trebuchet abcd",sizeof(line));
  tt_int_op(parse_smethod_line(line, mp), OP_LT, 0);

  reset_mp(mp);

  /* cowwect */
  strlcpy(line,"SMETHOD trebuchy 127.0.0.2:2999",sizeof(line));
  tt_int_op(parse_smethod_line(line, mp), OP_EQ, 0);
  tt_int_op(smartlist_len(mp->transports), OP_EQ, 1);
  transport = smartlist_get(mp->transports, 0);
  /* test registered address of transport */
  tor_addr_parse(&test_addr, "127.0.0.2");
  tt_assert(tor_addr_eq(&test_addr, &transport->addr));
  /* test registered port of transport */
  tt_uint_op(transport->port, OP_EQ, 2999);
  /* test registered name of transport */
  tt_str_op(transport->name,OP_EQ, "trebuchy");

  reset_mp(mp);

  /* Include some arguments. Good ones. */
  strlcpy(line,"SMETHOD trebuchet 127.0.0.1:9999 "
          "ARGS:counterweight=3,sling=snappy",
          sizeof(line));
  tt_int_op(parse_smethod_line(line, mp), OP_EQ, 0);
  tt_int_op(1, OP_EQ, smartlist_len(mp->transports));
  {
    const transport_t *transport_ = smartlist_get(mp->transports, 0);
    tt_assert(transport_);
    tt_str_op(transport_->name, OP_EQ, "trebuchet");
    tt_int_op(transport_->port, OP_EQ, 9999);
    tt_str_op(fmt_addr(&transport_->addr), OP_EQ, "127.0.0.1");
    tt_str_op(transport_->extra_info_args, OP_EQ,
              "counterweight=3,sling=snappy");
  }
  reset_mp(mp);

  /* unsupported version */
  strlcpy(line,"VERSION 666",sizeof(line));
  tt_int_op(parse_version(line, mp), OP_LT, 0);

  /* incomplete VERSION */
  strlcpy(line,"VERSION ",sizeof(line));
  tt_int_op(parse_version(line, mp), OP_LT, 0);

  /* correct VERSION */
  strlcpy(line,"VERSION 1",sizeof(line));
  tt_int_op(parse_version(line, mp), OP_EQ, 0);

 done:
  reset_mp(mp);
  smartlist_free(mp->transports);
  tor_free(mp);
}

static void
test_pt_get_transport_options(void *arg)
{
  char **execve_args;
  smartlist_t *transport_list = smartlist_new();
  managed_proxy_t *mp;
  or_options_t *options = get_options_mutable();
  char *opt_str = NULL;
  config_line_t *cl = NULL;
  (void)arg;

  execve_args = tor_malloc(sizeof(char*)*2);
  execve_args[0] = tor_strdup("cheeseshop");
  execve_args[1] = NULL;

  mp = managed_proxy_create(transport_list, execve_args, 1);
  tt_ptr_op(mp, OP_NE, NULL);
  opt_str = get_transport_options_for_server_proxy(mp);
  tt_ptr_op(opt_str, OP_EQ, NULL);

  smartlist_add_strdup(mp->transports_to_launch, "gruyere");
  smartlist_add_strdup(mp->transports_to_launch, "roquefort");
  smartlist_add_strdup(mp->transports_to_launch, "stnectaire");

  tt_assert(options);

  cl = tor_malloc_zero(sizeof(config_line_t));
  cl->value = tor_strdup("gruyere melty=10 hardness=se;ven");
  options->ServerTransportOptions = cl;

  cl = tor_malloc_zero(sizeof(config_line_t));
  cl->value = tor_strdup("stnectaire melty=4 hardness=three");
  cl->next = options->ServerTransportOptions;
  options->ServerTransportOptions = cl;

  cl = tor_malloc_zero(sizeof(config_line_t));
  cl->value = tor_strdup("pepperjack melty=12 hardness=five");
  cl->next = options->ServerTransportOptions;
  options->ServerTransportOptions = cl;

  opt_str = get_transport_options_for_server_proxy(mp);
  tt_str_op(opt_str, OP_EQ,
            "gruyere:melty=10;gruyere:hardness=se\\;ven;"
            "stnectaire:melty=4;stnectaire:hardness=three");

 done:
  tor_free(opt_str);
  config_free_lines(cl);
  managed_proxy_destroy(mp, 0);
  smartlist_free(transport_list);
}

static void
test_pt_protocol(void *arg)
{
  char line[200];

  managed_proxy_t *mp = tor_malloc_zero(sizeof(managed_proxy_t));
  (void)arg;
  mp->conf_state = PT_PROTO_LAUNCHED;
  mp->transports = smartlist_new();
  mp->argv = tor_calloc(2, sizeof(char *));
  mp->argv[0] = tor_strdup("<testcase>");

  /* various wrong protocol runs: */

  strlcpy(line,"VERSION 1",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_ACCEPTING_METHODS);

  strlcpy(line,"VERSION 1",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_BROKEN);

  reset_mp(mp);

  strlcpy(line,"CMETHOD trebuchet socks5 127.0.0.1:1999",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_BROKEN);

  reset_mp(mp);

  /* correct protocol run: */
  strlcpy(line,"VERSION 1",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_ACCEPTING_METHODS);

  strlcpy(line,"CMETHOD trebuchet socks5 127.0.0.1:1999",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_ACCEPTING_METHODS);

  strlcpy(line,"CMETHODS DONE",sizeof(line));
  handle_proxy_line(line, mp);
  tt_assert(mp->conf_state == PT_PROTO_CONFIGURED);

 done:
  reset_mp(mp);
  smartlist_free(mp->transports);
  tor_free(mp->argv[0]);
  tor_free(mp->argv);
  tor_free(mp);
}

static void
test_pt_get_extrainfo_string(void *arg)
{
  managed_proxy_t *mp1 = NULL, *mp2 = NULL;
  char **argv1, **argv2;
  smartlist_t *t1 = smartlist_new(), *t2 = smartlist_new();
  int r;
  char *s = NULL;
  (void) arg;

  argv1 = tor_malloc_zero(sizeof(char*)*3);
  argv1[0] = tor_strdup("ewige");
  argv1[1] = tor_strdup("Blumenkraft");
  argv1[2] = NULL;
  argv2 = tor_malloc_zero(sizeof(char*)*4);
  argv2[0] = tor_strdup("und");
  argv2[1] = tor_strdup("ewige");
  argv2[2] = tor_strdup("Schlangenkraft");
  argv2[3] = NULL;

  mp1 = managed_proxy_create(t1, argv1, 1);
  mp2 = managed_proxy_create(t2, argv2, 1);

  r = parse_smethod_line("SMETHOD hagbard 127.0.0.1:5555", mp1);
  tt_int_op(r, OP_EQ, 0);
  r = parse_smethod_line("SMETHOD celine 127.0.0.1:1723 ARGS:card=no-enemy",
                         mp2);
  tt_int_op(r, OP_EQ, 0);

  /* Force these proxies to look "completed" or they won't generate output. */
  mp1->conf_state = mp2->conf_state = PT_PROTO_COMPLETED;

  s = pt_get_extra_info_descriptor_string();
  tt_assert(s);
  tt_str_op(s, OP_EQ,
            "transport hagbard 127.0.0.1:5555\n"
            "transport celine 127.0.0.1:1723 card=no-enemy\n");

 done:
  /* XXXX clean up better */
  smartlist_free(t1);
  smartlist_free(t2);
  tor_free(s);
}

static int
process_read_stdout_replacement(process_t *process, buf_t *buffer)
{
  (void)process;
  static int times_called = 0;

  /* Generate some dummy CMETHOD lines the first 5 times. The 6th
     time, send 'CMETHODS DONE' to finish configuring the proxy. */
  times_called++;

  if (times_called <= 5) {
    buf_add_printf(buffer, "SMETHOD mock%d 127.0.0.1:555%d\n",
                           times_called, times_called);
  } else if (times_called <= 6) {
    buf_add_string(buffer, "SMETHODS DONE\n");
  } else if (times_called <= 7) {
    buf_add_string(buffer, "LOG SEVERITY=error MESSAGE=\"Oh noes, something "
                           "bad happened. What do we do!?\"\n");
    buf_add_string(buffer, "LOG SEVERITY=warning MESSAGE=\"warning msg\"\n");
    buf_add_string(buffer, "LOG SEVERITY=notice MESSAGE=\"notice msg\"\n");
    buf_add_string(buffer, "LOG SEVERITY=info MESSAGE=\"info msg\"\n");
    buf_add_string(buffer, "LOG SEVERITY=debug MESSAGE=\"debug msg\"\n");
  } else if (times_called <= 8) {
    buf_add_string(buffer, "STATUS TRANSPORT=a K_1=a K_2=b K_3=\"foo bar\"\n");
    buf_add_string(buffer, "STATUS TRANSPORT=b K_1=a K_2=b K_3=\"foo bar\"\n");
    buf_add_string(buffer, "STATUS TRANSPORT=c K_1=a K_2=b K_3=\"foo bar\"\n");
  }

  return (int)buf_datalen(buffer);
}

static or_state_t *dummy_state = NULL;

static or_state_t *
get_or_state_replacement(void)
{
  return dummy_state;
}

static int controlevent_n = 0;
static uint16_t controlevent_event = 0;
static smartlist_t *controlevent_msgs = NULL;

static void
queue_control_event_string_replacement(uint16_t event, char *msg)
{
  ++controlevent_n;
  controlevent_event = event;
  if (!controlevent_msgs)
    controlevent_msgs = smartlist_new();
  smartlist_add(controlevent_msgs, msg);
}

/* Test the configure_proxy() function. */
static void
test_pt_configure_proxy(void *arg)
{
  int i, retval;
  managed_proxy_t *mp = NULL;
  (void) arg;

  dummy_state = or_state_new();

  MOCK(process_read_stdout, process_read_stdout_replacement);
  MOCK(get_or_state,
       get_or_state_replacement);
  MOCK(queue_control_event_string,
       queue_control_event_string_replacement);

  control_testing_set_global_event_mask(EVENT_TRANSPORT_LAUNCHED);

  mp = tor_malloc_zero(sizeof(managed_proxy_t));
  mp->conf_state = PT_PROTO_ACCEPTING_METHODS;
  mp->transports = smartlist_new();
  mp->transports_to_launch = smartlist_new();
  mp->argv = tor_malloc_zero(sizeof(char*)*2);
  mp->argv[0] = tor_strdup("<testcase>");
  mp->is_server = 1;

  /* Configure the process. */
  mp->process = process_new("");
  process_set_stdout_read_callback(mp->process, managed_proxy_stdout_callback);
  process_set_data(mp->process, mp);

  /* Test the return value of configure_proxy() by calling it some
     times while it is uninitialized and then finally finalizing its
     configuration. */
  for (i = 0 ; i < 5 ; i++) {
    /* force a read from our mocked stdout reader. */
    process_notify_event_stdout(mp->process);
    /* try to configure our proxy. */
    retval = configure_proxy(mp);
    /* retval should be zero because proxy hasn't finished configuring yet */
    tt_int_op(retval, OP_EQ, 0);
    /* check the number of registered transports */
    tt_int_op(smartlist_len(mp->transports), OP_EQ, i+1);
    /* check that the mp is still waiting for transports */
    tt_assert(mp->conf_state == PT_PROTO_ACCEPTING_METHODS);
  }

  /* Get the SMETHOD DONE written to the process. */
  process_notify_event_stdout(mp->process);

  /* this last configure_proxy() should finalize the proxy configuration. */
  retval = configure_proxy(mp);
  /* retval should be 1 since the proxy finished configuring */
  tt_int_op(retval, OP_EQ, 1);
  /* check the mp state */
  tt_assert(mp->conf_state == PT_PROTO_COMPLETED);

  tt_int_op(controlevent_n, OP_EQ, 5);
  tt_int_op(controlevent_event, OP_EQ, EVENT_TRANSPORT_LAUNCHED);
  tt_int_op(smartlist_len(controlevent_msgs), OP_EQ, 5);
  smartlist_sort_strings(controlevent_msgs);
  tt_str_op(smartlist_get(controlevent_msgs, 0), OP_EQ,
            "650 TRANSPORT_LAUNCHED server mock1 127.0.0.1 5551\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 1), OP_EQ,
            "650 TRANSPORT_LAUNCHED server mock2 127.0.0.1 5552\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 2), OP_EQ,
            "650 TRANSPORT_LAUNCHED server mock3 127.0.0.1 5553\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 3), OP_EQ,
            "650 TRANSPORT_LAUNCHED server mock4 127.0.0.1 5554\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 4), OP_EQ,
            "650 TRANSPORT_LAUNCHED server mock5 127.0.0.1 5555\r\n");

  /* Get the log message out. */
  setup_full_capture_of_logs(LOG_ERR);
  process_notify_event_stdout(mp->process);
  expect_single_log_msg_containing("Oh noes, something bad happened");
  teardown_capture_of_logs();

  tt_int_op(controlevent_n, OP_EQ, 10);
  tt_int_op(controlevent_event, OP_EQ, EVENT_PT_LOG);
  tt_int_op(smartlist_len(controlevent_msgs), OP_EQ, 10);
  tt_str_op(smartlist_get(controlevent_msgs, 5), OP_EQ,
            "650 PT_LOG PT=<testcase> SEVERITY=error "
            "MESSAGE=\"Oh noes, "
            "something bad happened. What do we do!?\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 6), OP_EQ,
            "650 PT_LOG PT=<testcase> SEVERITY=warning "
            "MESSAGE=\"warning msg\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 7), OP_EQ,
            "650 PT_LOG PT=<testcase> SEVERITY=notice "
            "MESSAGE=\"notice msg\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 8), OP_EQ,
            "650 PT_LOG PT=<testcase> SEVERITY=info "
            "MESSAGE=\"info msg\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 9), OP_EQ,
            "650 PT_LOG PT=<testcase> SEVERITY=debug "
            "MESSAGE=\"debug msg\"\r\n");

  /* Get the STATUS messages out. */
  process_notify_event_stdout(mp->process);

  tt_int_op(controlevent_n, OP_EQ, 13);
  tt_int_op(controlevent_event, OP_EQ, EVENT_PT_STATUS);
  tt_int_op(smartlist_len(controlevent_msgs), OP_EQ, 13);

  tt_str_op(smartlist_get(controlevent_msgs, 10), OP_EQ,
            "650 PT_STATUS "
            "PT=<testcase> TRANSPORT=a K_1=a K_2=b K_3=\"foo bar\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 11), OP_EQ,
            "650 PT_STATUS "
            "PT=<testcase> TRANSPORT=b K_1=a K_2=b K_3=\"foo bar\"\r\n");
  tt_str_op(smartlist_get(controlevent_msgs, 12), OP_EQ,
            "650 PT_STATUS "
            "PT=<testcase> TRANSPORT=c K_1=a K_2=b K_3=\"foo bar\"\r\n");

  { /* check that the transport info were saved properly in the tor state */
    config_line_t *transport_in_state = NULL;
    smartlist_t *transport_info_sl = smartlist_new();
    char *name_of_transport = NULL;
    char *bindaddr = NULL;

    /* Get the bindaddr for "mock1" and check it against the bindaddr
       that the mocked tor_get_lines_from_handle() generated. */
    transport_in_state = get_transport_in_state_by_name("mock1");
    tt_assert(transport_in_state);
    smartlist_split_string(transport_info_sl, transport_in_state->value,
                           NULL, 0, 0);
    name_of_transport = smartlist_get(transport_info_sl, 0);
    bindaddr = smartlist_get(transport_info_sl, 1);
    tt_str_op(name_of_transport, OP_EQ, "mock1");
    tt_str_op(bindaddr, OP_EQ, "127.0.0.1:5551");

    SMARTLIST_FOREACH(transport_info_sl, char *, cp, tor_free(cp));
    smartlist_free(transport_info_sl);
  }

 done:
  teardown_capture_of_logs();
  or_state_free(dummy_state);
  UNMOCK(process_read_stdout);
  UNMOCK(get_or_state);
  UNMOCK(queue_control_event_string);
  if (controlevent_msgs) {
    SMARTLIST_FOREACH(controlevent_msgs, char *, cp, tor_free(cp));
    smartlist_free(controlevent_msgs);
    controlevent_msgs = NULL;
  }
  if (mp->transports) {
    SMARTLIST_FOREACH(mp->transports, transport_t *, t, transport_free(t));
    smartlist_free(mp->transports);
  }
  smartlist_free(mp->transports_to_launch);
  process_free(mp->process);
  tor_free(mp->argv[0]);
  tor_free(mp->argv);
  tor_free(mp);
}

/* Test the get_pt_proxy_uri() function. */
static void
test_get_pt_proxy_uri(void *arg)
{
  or_options_t *options = get_options_mutable();
  char *uri = NULL;
  int ret;
  (void) arg;

  /* Test with no proxy. */
  uri = get_pt_proxy_uri();
  tt_ptr_op(uri, OP_EQ, NULL);

  /* Test with a SOCKS4 proxy. */
  options->Socks4Proxy = tor_strdup("192.0.2.1:1080");
  ret = tor_addr_port_lookup(options->Socks4Proxy,
                             &options->Socks4ProxyAddr,
                             &options->Socks4ProxyPort);
  tt_int_op(ret, OP_EQ, 0);
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "socks4a://192.0.2.1:1080");
  tor_free(uri);
  tor_free(options->Socks4Proxy);

  /* Test with a SOCKS5 proxy, no username/password. */
  options->Socks5Proxy = tor_strdup("192.0.2.1:1080");
  ret = tor_addr_port_lookup(options->Socks5Proxy,
                             &options->Socks5ProxyAddr,
                             &options->Socks5ProxyPort);
  tt_int_op(ret, OP_EQ, 0);
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "socks5://192.0.2.1:1080");
  tor_free(uri);

  /* Test with a SOCKS5 proxy, with username/password. */
  options->Socks5ProxyUsername = tor_strdup("hwest");
  options->Socks5ProxyPassword = tor_strdup("r34n1m470r");
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "socks5://hwest:r34n1m470r@192.0.2.1:1080");
  tor_free(uri);
  tor_free(options->Socks5Proxy);
  tor_free(options->Socks5ProxyUsername);
  tor_free(options->Socks5ProxyPassword);

  /* Test with a HTTPS proxy, no authenticator. */
  options->HTTPSProxy = tor_strdup("192.0.2.1:80");
  ret = tor_addr_port_lookup(options->HTTPSProxy,
                             &options->HTTPSProxyAddr,
                             &options->HTTPSProxyPort);
  tt_int_op(ret, OP_EQ, 0);
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "http://192.0.2.1:80");
  tor_free(uri);

  /* Test with a HTTPS proxy, with authenticator. */
  options->HTTPSProxyAuthenticator = tor_strdup("hwest:r34n1m470r");
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "http://hwest:r34n1m470r@192.0.2.1:80");
  tor_free(uri);
  tor_free(options->HTTPSProxy);
  tor_free(options->HTTPSProxyAuthenticator);

  /* Token nod to the fact that IPv6 exists. */
  options->Socks4Proxy = tor_strdup("[2001:db8::1]:1080");
  ret = tor_addr_port_lookup(options->Socks4Proxy,
                             &options->Socks4ProxyAddr,
                             &options->Socks4ProxyPort);
  tt_int_op(ret, OP_EQ, 0);
  uri = get_pt_proxy_uri();
  tt_str_op(uri, OP_EQ, "socks4a://[2001:db8::1]:1080");
  tor_free(uri);
  tor_free(options->Socks4Proxy);

 done:
  if (uri)
    tor_free(uri);
}

#define PT_LEGACY(name)                                               \
  { #name, test_pt_ ## name , 0, NULL, NULL }

struct testcase_t pt_tests[] = {
  PT_LEGACY(parsing),
  PT_LEGACY(protocol),
  { "get_transport_options", test_pt_get_transport_options, TT_FORK,
    NULL, NULL },
  { "get_extrainfo_string", test_pt_get_extrainfo_string, TT_FORK,
    NULL, NULL },
  { "configure_proxy",test_pt_configure_proxy, TT_FORK,
    NULL, NULL },
  { "get_pt_proxy_uri", test_get_pt_proxy_uri, TT_FORK,
    NULL, NULL },
  END_OF_TESTCASES
};
