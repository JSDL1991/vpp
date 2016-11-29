/*
 * Copyright (c) 2016 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 *------------------------------------------------------------------
 * l2sess.c - simple MAC-swap API / debug CLI handling
 *------------------------------------------------------------------
 */

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <l2sess/l2sess.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlibsocket/api.h>
#include <vppinfra/timing_wheel.h>

#include <vnet/l2/l2_output.h>
#include <vnet/l2/l2_input.h>

/* define message IDs */
#include <l2sess/l2sess_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <l2sess/l2sess_all_api_h.h>
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <l2sess/l2sess_all_api_h.h>
#undef vl_endianfun

/* instantiate all the print functions we know about */
#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)
#define vl_printfun
#include <l2sess/l2sess_all_api_h.h>
#undef vl_printfun

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <l2sess/l2sess_all_api_h.h>
#undef vl_api_version

/*
 * A handy macro to set up a message reply.
 * Assumes that the following variables are available:
 * mp - pointer to request message
 * rmp - pointer to reply message type
 * rv - return value
 */

#define REPLY_MACRO(t)                                          \
do {                                                            \
    unix_shared_memory_queue_t * q =                            \
    vl_api_client_index_to_input_queue (mp->client_index);      \
    if (!q)                                                     \
        return;                                                 \
                                                                \
    rmp = vl_msg_api_alloc (sizeof (*rmp));                     \
    rmp->_vl_msg_id = ntohs((t)+sm->msg_id_base);               \
    rmp->context = mp->context;                                 \
    rmp->retval = ntohl(rv);                                    \
                                                                \
    vl_msg_api_send_shmem (q, (u8 *)&rmp);                      \
} while(0);


/* List of message types that this plugin understands */

#define foreach_l2sess_plugin_api_msg                           \
_(L2SESS_BIND_TABLES, l2sess_bind_tables)

/*
 * This routine exists to convince the vlib plugin framework that
 * we haven't accidentally copied a random .dll into the plugin directory.
 *
 * Also collects global variable pointers passed from the vpp engine
 */

clib_error_t *
vlib_plugin_register (vlib_main_t * vm, vnet_plugin_handoff_t * h,
		      int from_early_init)
{
  l2sess_main_t *sm = &l2sess_main;
  clib_error_t *error = 0;
  memset (sm, 0, sizeof (*sm));

  sm->vlib_main = vm;
  sm->vnet_main = h->vnet_main;
  sm->ethernet_main = h->ethernet_main;

  return error;
}


/*
void l2sess_add_next_with_slot_to_all_output(vlib_main_t *vm, uword next_node_index, uword slot) {
#define _(node_name, node_var, is_out, is_ip6, is_track) \
  if (is_out) vlib_node_add_next_with_slot(vm, node_var.index, next_node_index, slot);
foreach_l2sess_node
#undef _
}

void l2sess_add_next_with_slot_to_all_input(vlib_main_t *vm, uword next_node_index, uword slot) {
#define _(node_name, node_var, is_out, is_ip6, is_track) \
  if (!is_out) vlib_node_add_next_with_slot(vm, node_var.index, next_node_index, slot);
foreach_l2sess_node
#undef _
}
*/

/*
void l2sess_init_next_features_output(vlib_main_t *vm, l2sess_main_t * sm) {
#define _(node_name, node_var, is_out, is_ip6, is_track) \
  if (is_out) feat_bitmap_init_next_nodes(vm, node_var.index, L2OUTPUT_N_FEAT,  l2output_get_feat_names (), sm->output_feat_next_node_index);
foreach_l2sess_node
#undef _
}
*/

void
l2sess_init_next_features_input (vlib_main_t * vm, l2sess_main_t * sm)
{
#define _(node_name, node_var, is_out, is_ip6, is_track) \
  if (!is_out) feat_bitmap_init_next_nodes(vm, node_var.index, L2INPUT_N_FEAT,  l2input_get_feat_names (), sm->node_var ## _input_next_node_index);
  foreach_l2sess_node
#undef _
}

void
l2sess_add_our_next_nodes (vlib_main_t * vm, l2sess_main_t * sm,
			   u8 * prev_node_name, int add_output_nodes)
{
  vlib_node_t *n;
  n = vlib_get_node_by_name (vm, prev_node_name);
#define _(node_name, node_var, is_out, is_ip6, is_track) \
  if (is_out == add_output_nodes) { \
    u32 idx = vlib_node_add_next_with_slot(vm, n->index, node_var.index, ~0); \
    if (is_track) { \
      sm->next_slot_track_node_by_is_ip6_is_out[is_ip6][is_out] = idx; \
    } \
  }
  foreach_l2sess_node
#undef _
}

void
l2sess_setup_nodes (void)
{
  vlib_main_t *vm = vlib_get_main ();
  l2sess_main_t *sm = &l2sess_main;

  l2sess_init_next_features_input (vm, sm);

  l2sess_add_our_next_nodes (vm, sm, (u8 *) "l2-input-classify", 0);
  l2sess_add_our_next_nodes (vm, sm, (u8 *) "l2-output-classify", 1);

}



static clib_error_t *
show_l2sess_bind_command_fn (vlib_main_t * vm,
			     unformat_input_t * input,
			     vlib_cli_command_t * cmd)
{
  l2sess_main_t *sm = &l2sess_main;
  u32 fwd_table_index = ~0;

  if (unformat (input, "%d", &fwd_table_index))
    {
      if (fwd_table_index < vec_len (sm->fwd_to_rev_by_table_index))
	{
	  vlib_cli_output (vm, "l2sess bind %d %d\n", fwd_table_index,
			   sm->fwd_to_rev_by_table_index[fwd_table_index]);
	}
      else
	{
	  vlib_cli_output (vm, "l2sess bind %d -1\n");
	}
    }
  for (fwd_table_index = 0;
       fwd_table_index < vec_len (sm->fwd_to_rev_by_table_index);
       fwd_table_index++)
    {
      vlib_cli_output (vm, "l2sess bind %d %d\n", fwd_table_index,
		       sm->fwd_to_rev_by_table_index[fwd_table_index]);
    }
  return 0;
}

VLIB_CLI_COMMAND (show_l2sess_bind_command, static) =
{
.path = "show l2sess bind",.short_help =
    "show l2sess bind [<table-index>]",.function =
    show_l2sess_bind_command_fn,};

/* Action function shared between message handler and debug CLI */

int
l2sess_bind_tables (l2sess_main_t * sm, u32 fwd_table_index,
		    u32 rev_table_index, u8 bind_type)
{
  int rv = 0;
  u32 old_fwd_table_index;
  u32 old_rev_table_index;

  if (~0 == fwd_table_index)
    {
      /* The first argument MUST be a valid table index */
      return -1;
    }
  vec_validate_init_empty (sm->fwd_to_rev_by_table_index, fwd_table_index,
			   ~0);

  if (~0 != rev_table_index)
    {
      vec_validate_init_empty (sm->fwd_to_rev_by_table_index, rev_table_index,
			       ~0);

      /* Both tables might already be potential members of other two bindings, break that first. */
      old_fwd_table_index = sm->fwd_to_rev_by_table_index[rev_table_index];
      old_rev_table_index = sm->fwd_to_rev_by_table_index[fwd_table_index];

      /* The validity of old_fwd_table_index and old_rev_table_index was verified when someone assigned them */
      if (~0 != old_fwd_table_index)
	{
	  sm->fwd_to_rev_by_table_index[old_fwd_table_index] = ~0;
	}
      if (~0 != old_rev_table_index)
	{
	  sm->fwd_to_rev_by_table_index[old_rev_table_index] = ~0;
	}

      sm->fwd_to_rev_by_table_index[fwd_table_index] = rev_table_index;
      sm->fwd_to_rev_by_table_index[rev_table_index] = fwd_table_index;
    }
  else
    {
      /* Break up the binding */
      rev_table_index = sm->fwd_to_rev_by_table_index[fwd_table_index];

      sm->fwd_to_rev_by_table_index[fwd_table_index] = ~0;
      sm->fwd_to_rev_by_table_index[rev_table_index] = ~0;
    }
  return rv;
}

static clib_error_t *
macswap_enable_disable_command_fn (vlib_main_t * vm,
				   unformat_input_t * input,
				   vlib_cli_command_t * cmd)
{
  l2sess_main_t *sm = &l2sess_main;
  int rv;
  u32 fwd_table_index;
  u32 rev_table_index;
  u8 bind_type = 0;

  if (!unformat (input, "%d", &fwd_table_index))
    {
      return clib_error_return (0, "forward table index required");
    }
  if (!unformat (input, "%d", &rev_table_index))
    {
      return clib_error_return (0, "reverse table index required");
    }

  rv = l2sess_bind_tables (sm, fwd_table_index, rev_table_index, bind_type);

  switch (rv)
    {
    case 0:
      break;

    default:
      return clib_error_return (0, "l2sess_bind_tables returned %d", rv);
    }
  return 0;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (l2sess_bind_command, static) = {
    .path = "l2sess bind",
    .short_help = "l2sess bind <table-fwd> <table-rev>",
    .function = macswap_enable_disable_command_fn,
};
/* *INDENT-ON* */

static char *
get_l4_proto_str (int is_ip6, uint8_t l4_proto)
{
  switch (l4_proto)
    {
    case 6:
      return "tcp";
    case 17:
      return "udp";
    case 1:
      return "icmp";
    case 58:
      return "icmp6";
    default:
      return "<?l4-unknown?>";
    }
}

static clib_error_t *
l2sess_show_command_fn (vlib_main_t * vm,
			unformat_input_t * input, vlib_cli_command_t * cmd)
{
  l2sess_main_t *sm = &l2sess_main;
  clib_time_t *ct = &vm->clib_time;
  l2s_session_t *s;
  u64 now = clib_cpu_time_now ();

  vlib_cli_output (vm, "Timing wheel info: \n%U", format_timing_wheel,
		   &sm->timing_wheel, 255);

  pool_foreach (s, sm->sessions, (
				   {
				   f64 ctime =
				   (now -
				    s->create_time) * ct->seconds_per_clock;
				   f64 atime0 =
				   (now -
				    s->side[0].active_time) *
				   ct->seconds_per_clock;
				   f64 atime1 =
				   (now -
				    s->side[1].active_time) *
				   ct->seconds_per_clock;
/*
    f64 ctime = (s->create_time - vm->cpu_time_main_loop_start) * ct->seconds_per_clock;
    f64 atime0 = (s->side[0].active_time - vm->cpu_time_main_loop_start) * ct->seconds_per_clock;
    f64 atime1 = (s->side[1].active_time - vm->cpu_time_main_loop_start) * ct->seconds_per_clock;
*/
				   u8 * out0 =
				   format (0,
					   "%5d: create time: %U pkts/bytes/active time: [ %ld %ld %U : %ld %ld %U ]\n",
					   (s - sm->sessions),
					   format_time_interval, "h:m:s:u",
					   ctime, s->side[0].n_packets,
					   s->side[0].n_bytes,
					   format_time_interval, "h:m:s:u",
					   atime0, s->side[1].n_packets,
					   s->side[1].n_bytes,
					   format_time_interval, "h:m:s:u",
					   atime1); u8 * out1 = 0;
				   if (s->is_ip6)
				   {
				   out1 =
				   format (0, "%s %U :%u <-> %U :%u",
					   get_l4_proto_str (s->is_ip6,
							     s->l4_proto),
					   format_ip6_address,
					   &s->side[0].addr.ip6,
					   s->side[0].port,
					   format_ip6_address,
					   &s->side[1].addr.ip6,
					   s->side[1].port);}
				   else
				   {
				   out1 =
				   format (0, "%s %U :%u <-> %U :%u",
					   get_l4_proto_str (s->is_ip6,
							     s->l4_proto),
					   format_ip4_address,
					   &s->side[0].addr.ip4,
					   s->side[0].port,
					   format_ip4_address,
					   &s->side[1].addr.ip4,
					   s->side[1].port);}
				   vlib_cli_output (vm, "%s       %s", out0,
						    out1); vec_free (out0);
				   vec_free (out1);}
		));
  return 0;
}

static clib_error_t *
l2sess_show_count_command_fn (vlib_main_t * vm,
			      unformat_input_t * input,
			      vlib_cli_command_t * cmd)
{
  l2sess_main_t *sm = &l2sess_main;

  vlib_cli_output (vm, "Timing wheel info: \n%U", format_timing_wheel,
		   &sm->timing_wheel, 255);
  vlib_cli_output (vm, "session pool len: %d, pool elts: %d",
		   pool_len (sm->sessions), pool_elts (sm->sessions));
  vlib_cli_output (vm,
		   "attempted to delete sessions which were already free: %d",
		   sm->counter_attempted_delete_free_session);
  return 0;
}


/* *INDENT-OFF* */
VLIB_CLI_COMMAND (l2sess_show_command, static) = {
    .path = "show l2sess",
    .short_help = "show l2sess",
    .function = l2sess_show_command_fn,
};

VLIB_CLI_COMMAND (l2sess_show_count_command, static) = {
    .path = "show l2sess count",
    .short_help = "show l2sess count",
    .function = l2sess_show_count_command_fn,
};
/* *INDENT-OFF* */

/* API message handler */
static void vl_api_l2sess_bind_tables_t_handler
(vl_api_l2sess_bind_tables_t * mp)
{
  vl_api_l2sess_bind_tables_reply_t * rmp;
  l2sess_main_t * sm = &l2sess_main;
  int rv;

  rv = l2sess_bind_tables (sm, ntohl(mp->fwd_table_index), ntohl(mp->rev_table_index), mp->bind_type);

  REPLY_MACRO(VL_API_L2SESS_BIND_TABLES_REPLY);
}

/* Set up the API message handling tables */
static clib_error_t *
l2sess_plugin_api_hookup (vlib_main_t *vm)
{
  l2sess_main_t * sm = &l2sess_main;
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + sm->msg_id_base),     \
                           #n,					\
                           vl_api_##n##_t_handler,              \
                           vl_noop_handler,                     \
                           vl_api_##n##_t_endian,               \
                           vl_api_##n##_t_print,                \
                           sizeof(vl_api_##n##_t), 1);
    foreach_l2sess_plugin_api_msg;
#undef _

    return 0;
}

static inline u64
time_sec_to_clock( clib_time_t *ct, f64 sec)
{
  return (u64)(((f64)sec)/ct->seconds_per_clock);
}

static clib_error_t * l2sess_init (vlib_main_t * vm)
{
  l2sess_main_t * sm = &l2sess_main;
  clib_error_t * error = 0;
  u8 * name;
  u64 cpu_time_now = clib_cpu_time_now();


  clib_time_t *ct = &vm->clib_time;
  sm->udp_session_idle_timeout = time_sec_to_clock(ct, UDP_SESSION_IDLE_TIMEOUT_SEC);
  sm->tcp_session_idle_timeout = time_sec_to_clock(ct, TCP_SESSION_IDLE_TIMEOUT_SEC);
  sm->tcp_session_transient_timeout = time_sec_to_clock(ct, TCP_SESSION_TRANSIENT_TIMEOUT_SEC);

  name = format (0, "l2sess_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  sm->msg_id_base = vl_msg_api_get_msg_ids
      ((char *) name, VL_MSG_FIRST_AVAILABLE);

  error = l2sess_plugin_api_hookup (vm);

  /* The min sched time of 10e-1 causes erroneous behavior... */
  sm->timing_wheel.min_sched_time = 10e-2;
  sm->timing_wheel.max_sched_time = 3600.0*48.0;
  timing_wheel_init (&sm->timing_wheel, cpu_time_now, vm->clib_time.clocks_per_second);
  sm->timer_wheel_next_expiring_time = 0;
  /* Pre-allocate expired nodes. */
  vec_alloc (sm->data_from_advancing_timing_wheel, 32);

  l2sess_setup_nodes();
  l2output_init_output_node_vec (&sm->output_next_nodes.output_node_index_vec);

  vec_free(name);

  return error;
}

VLIB_INIT_FUNCTION (l2sess_init);


