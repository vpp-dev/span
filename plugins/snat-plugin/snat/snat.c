/*
 * snat.c - simple nat plugin
 *
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

#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <vlibapi/api.h>
#include <snat/snat.h>

#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlibsocket/api.h>

snat_main_t snat_main;

/* define message IDs */
#include <snat/snat_msg_enum.h>

/* define message structures */
#define vl_typedefs
#include <snat/snat_all_api_h.h> 
#undef vl_typedefs

/* define generated endian-swappers */
#define vl_endianfun
#include <snat/snat_all_api_h.h> 
#undef vl_endianfun

#define vl_print(handle, ...) vlib_cli_output (handle, __VA_ARGS__)

/* Get the API version number */
#define vl_api_version(n,v) static u32 api_version=(v);
#include <snat/snat_all_api_h.h>
#undef vl_api_version

/* Macro to finish up custom dump fns */
#define FINISH                                  \
    vec_add1 (s, 0);                            \
    vl_print (handle, (char *)s);               \
    vec_free (s);                               \
    return handle;

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

#define REPLY_MACRO2(t, body)                                   \
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
    do {body;} while (0);                                       \
    vl_msg_api_send_shmem (q, (u8 *)&rmp);                      \
} while(0);


/* Hook up input features */
VNET_IP4_UNICAST_FEATURE_INIT (ip4_snat_in2out, static) = {
  .node_name = "snat-in2out",
  .runs_before = (char *[]){"snat-out2in", 0},
  .feature_index = &snat_main.rx_feature_in2out,
};
VNET_IP4_UNICAST_FEATURE_INIT (ip4_snat_out2in, static) = {
  .node_name = "snat-out2in",
  .runs_before = (char *[]){"ip4-lookup", 0},
  .feature_index = &snat_main.rx_feature_out2in,
};
VNET_IP4_UNICAST_FEATURE_INIT (ip4_snat_in2out_fast, static) = {
  .node_name = "snat-in2out-fast",
  .runs_before = (char *[]){"snat-out2in-fast", 0},
  .feature_index = &snat_main.rx_feature_in2out_fast,
};
VNET_IP4_UNICAST_FEATURE_INIT (ip4_snat_out2in_fast, static) = {
  .node_name = "snat-out2in-fast",
  .runs_before = (char *[]){"ip4-lookup", 0},
  .feature_index = &snat_main.rx_feature_out2in_fast,
};


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
  snat_main_t * sm = &snat_main;
  clib_error_t * error = 0;

  sm->vlib_main = vm;
  sm->vnet_main = h->vnet_main;
  sm->ethernet_main = h->ethernet_main;

  return error;
}

/*$$$$$ move to an installed header file */
#if (1 || CLIB_DEBUG > 0)       /* "trust, but verify" */

#define VALIDATE_SW_IF_INDEX(mp)				\
 do { u32 __sw_if_index = ntohl(mp->sw_if_index);		\
    vnet_main_t *__vnm = vnet_get_main();                       \
    if (pool_is_free_index(__vnm->interface_main.sw_interfaces, \
                           __sw_if_index)) {                    \
        rv = VNET_API_ERROR_INVALID_SW_IF_INDEX;                \
        goto bad_sw_if_index;                                   \
    }                                                           \
} while(0);

#define BAD_SW_IF_INDEX_LABEL                   \
do {                                            \
bad_sw_if_index:                                \
    ;                                           \
} while (0);

#define VALIDATE_RX_SW_IF_INDEX(mp)				\
 do { u32 __rx_sw_if_index = ntohl(mp->rx_sw_if_index);		\
    vnet_main_t *__vnm = vnet_get_main();                       \
    if (pool_is_free_index(__vnm->interface_main.sw_interfaces, \
                           __rx_sw_if_index)) {			\
        rv = VNET_API_ERROR_INVALID_SW_IF_INDEX;                \
        goto bad_rx_sw_if_index;				\
    }                                                           \
} while(0);

#define BAD_RX_SW_IF_INDEX_LABEL		\
do {                                            \
bad_rx_sw_if_index:				\
    ;                                           \
} while (0);

#define VALIDATE_TX_SW_IF_INDEX(mp)				\
 do { u32 __tx_sw_if_index = ntohl(mp->tx_sw_if_index);		\
    vnet_main_t *__vnm = vnet_get_main();                       \
    if (pool_is_free_index(__vnm->interface_main.sw_interfaces, \
                           __tx_sw_if_index)) {			\
        rv = VNET_API_ERROR_INVALID_SW_IF_INDEX;                \
        goto bad_tx_sw_if_index;				\
    }                                                           \
} while(0);

#define BAD_TX_SW_IF_INDEX_LABEL		\
do {                                            \
bad_tx_sw_if_index:				\
    ;                                           \
} while (0);

#else

#define VALIDATE_SW_IF_INDEX(mp)
#define BAD_SW_IF_INDEX_LABEL
#define VALIDATE_RX_SW_IF_INDEX(mp)
#define BAD_RX_SW_IF_INDEX_LABEL
#define VALIDATE_TX_SW_IF_INDEX(mp)
#define BAD_TX_SW_IF_INDEX_LABEL

#endif  /* CLIB_DEBUG > 0 */

void snat_add_address (snat_main_t *sm, ip4_address_t *addr)
{
  snat_address_t * ap;

  vec_add2 (sm->addresses, ap, 1);
  ap->addr = *addr;

}

static void increment_v4_address (ip4_address_t * a)
{
  u32 v;
  
  v = clib_net_to_host_u32(a->as_u32) + 1;
  a->as_u32 = clib_host_to_net_u32(v);
}

/**
 * @brief Add static mapping.
 *
 * Create static mapping between local addr+port and external addr+port.
 *
 * @param l_addr Local IPv4 address.
 * @param e_addr External IPv4 address.
 * @param l_port Local port number.
 * @param e_port External port number.
 * @param vrf_id VRF ID.
 * @param addr_only If 0 address port and pair mapping, otherwise address only.
 * @param is_add If 0 delete static mapping, otherwise add.
 *
 * @returns
 */
int snat_add_static_mapping(ip4_address_t l_addr, ip4_address_t e_addr,
                            u16 l_port, u16 e_port, u32 vrf_id, int addr_only,
                            int is_add)
{
  snat_main_t * sm = &snat_main;
  snat_static_mapping_t *m;
  snat_static_mapping_key_t m_key;
  clib_bihash_kv_8_8_t kv, value;
  snat_address_t *a = 0;
  u32 fib_index = ~0;
  uword * p;
  int i;

  m_key.addr = e_addr;
  m_key.port = addr_only ? 0 : e_port;
  m_key.pad = 0;
  kv.key = m_key.as_u64;
  if (clib_bihash_search_8_8 (&sm->static_mapping_by_external, &kv, &value))
    m = 0;
  else
    m = pool_elt_at_index (sm->static_mappings, value.value);

  if (is_add)
    {
      if (m)
        return VNET_API_ERROR_VALUE_EXIST;

      /* Convert VRF id to FIB index */
      if (vrf_id != ~0)
        {
          p = hash_get (sm->ip4_main->fib_index_by_table_id, vrf_id);
          if (!p)
            return VNET_API_ERROR_NO_SUCH_FIB;
          fib_index = p[0];
        }
      /* If not specified use inside VRF id from SNAT plugin startup config */
      else
        {
          if (sm->inside_fib_index == ~0)
            {
              p = hash_get (sm->ip4_main->fib_index_by_table_id, sm->inside_vrf_id);
              if (!p)
                return VNET_API_ERROR_NO_SUCH_FIB;
              fib_index = p[0];
              sm->inside_fib_index = fib_index;
            }
          else
            fib_index = sm->inside_fib_index;

          vrf_id = sm->inside_vrf_id;
        }

      /* If outside FIB index is not resolved yet */
      if (sm->outside_fib_index == ~0)
        {
          p = hash_get (sm->ip4_main->fib_index_by_table_id, sm->outside_vrf_id);
          if (!p)
            return VNET_API_ERROR_NO_SUCH_FIB;
          sm->outside_fib_index = p[0];
        }

      /* Find external address in allocated addresses and reserve port for
         address and port pair mapping when dynamic translations enabled */
      if (!addr_only && !(sm->static_mapping_only))
        {
          for (i = 0; i < vec_len (sm->addresses); i++)
            {
              if (sm->addresses[i].addr.as_u32 == e_addr.as_u32)
                {
                  a = sm->addresses + i;
                  /* External port must be unused */
                  if (clib_bitmap_get (a->busy_port_bitmap, e_port))
                    return VNET_API_ERROR_INVALID_VALUE;
                  a->busy_port_bitmap = clib_bitmap_set (a->busy_port_bitmap,
                                                         e_port, 1);
                  if (e_port > 1024)
                    a->busy_ports++;

                  break;
                }
            }
          /* External address must be allocated */
          if (!a)
            return VNET_API_ERROR_NO_SUCH_ENTRY;
        }

      pool_get (sm->static_mappings, m);
      memset (m, 0, sizeof (*m));
      m->local_addr = l_addr;
      m->external_addr = e_addr;
      m->addr_only = addr_only;
      m->vrf_id = vrf_id;
      m->fib_index = fib_index;
      if (!addr_only)
        {
          m->local_port = l_port;
          m->external_port = e_port;
        }

      m_key.addr = m->local_addr;
      m_key.port = m->local_port;
      m_key.pad = 0;
      kv.key = m_key.as_u64;
      kv.value = m - sm->static_mappings;
      clib_bihash_add_del_8_8(&sm->static_mapping_by_local, &kv, 1);

      m_key.addr = m->external_addr;
      m_key.port = m->external_port;
      kv.key = m_key.as_u64;
      kv.value = m - sm->static_mappings;
      clib_bihash_add_del_8_8(&sm->static_mapping_by_external, &kv, 1);
    }
  else
    {
      if (!m)
        return VNET_API_ERROR_NO_SUCH_ENTRY;

      /* Free external address port */
      if (!addr_only && !(sm->static_mapping_only))
        {
          for (i = 0; i < vec_len (sm->addresses); i++)
            {
              if (sm->addresses[i].addr.as_u32 == e_addr.as_u32)
                {
                  a = sm->addresses + i;
                  a->busy_port_bitmap = clib_bitmap_set (a->busy_port_bitmap,
                                                         e_port, 0);
                  a->busy_ports--;

                  break;
                }
            }
        }

      m_key.addr = m->local_addr;
      m_key.port = m->local_port;
      m_key.pad = 0;
      kv.key = m_key.as_u64;
      clib_bihash_add_del_8_8(&sm->static_mapping_by_local, &kv, 0);

      m_key.addr = m->external_addr;
      m_key.port = m->external_port;
      kv.key = m_key.as_u64;
      clib_bihash_add_del_8_8(&sm->static_mapping_by_external, &kv, 0);

      /* Delete session(s) for static mapping if exist */
      if (!(sm->static_mapping_only) ||
          (sm->static_mapping_only && sm->static_mapping_connection_tracking))
        {
          snat_user_key_t u_key;
          snat_user_t *u;
          dlist_elt_t * head, * elt;
          u32 elt_index, head_index;
          u32 ses_index;
          snat_session_t * s;

          u_key.addr = m->local_addr;
          u_key.fib_index = m->fib_index;
          kv.key = u_key.as_u64;
          if (!clib_bihash_search_8_8 (&sm->user_hash, &kv, &value))
            {
              u = pool_elt_at_index (sm->users, value.value);
              if (u->nstaticsessions)
                {
                  head_index = u->sessions_per_user_list_head_index;
                  head = pool_elt_at_index (sm->list_pool, head_index);
                  elt_index = head->next;
                  elt = pool_elt_at_index (sm->list_pool, elt_index);
                  ses_index = elt->value;
                  while (ses_index != ~0)
                    {
                      s =  pool_elt_at_index (sm->sessions, ses_index);

                      if (!addr_only)
                        {
                          if ((s->out2in.addr.as_u32 != e_addr.as_u32) &&
                              (clib_net_to_host_u16 (s->out2in.port) != e_port))
                            continue;
                        }
                      value.key = s->in2out.as_u64;
                      clib_bihash_add_del_8_8 (&sm->in2out, &value, 0);
                      value.key = s->out2in.as_u64;
                      clib_bihash_add_del_8_8 (&sm->out2in, &value, 0);
                      pool_put (sm->sessions, s);

                      if (!addr_only)
                        break;

                      elt_index = elt->next;
                      elt = pool_elt_at_index (sm->list_pool, elt_index);
                      ses_index = elt->value;
                    }
                  if (addr_only)
                    {
                      while ((elt_index = clib_dlist_remove_head(sm->list_pool, head_index)) != ~0)
                        pool_put_index (sm->list_pool, elt_index);
                      pool_put (sm->users, u);
                      clib_bihash_add_del_8_8 (&sm->user_hash, &kv, 0);
                    }
                  else
                    {
                      if (ses_index != ~0)
                        {
                          clib_dlist_remove (sm->list_pool, elt_index);
                          pool_put (sm->list_pool, elt);
                          u->nstaticsessions--;
                        }
                    }
                }
            }
        }

      /* Delete static mapping from pool */
      pool_put (sm->static_mappings, m);
    }

  return 0;
}

static void 
vl_api_snat_add_address_range_t_handler
(vl_api_snat_add_address_range_t * mp)
{
  snat_main_t * sm = &snat_main;
  vl_api_snat_add_address_range_reply_t * rmp;
  ip4_address_t this_addr;
  u32 start_host_order, end_host_order;
  int i, count;
  int rv = 0;
  u32 * tmp;

  if (mp->is_ip4 != 1)
    {
      rv = VNET_API_ERROR_UNIMPLEMENTED;
      goto send_reply;
    }

  if (sm->static_mapping_only)
    {
      rv = VNET_API_ERROR_FEATURE_DISABLED;
      goto send_reply;
    }

  tmp = (u32 *) mp->first_ip_address;
  start_host_order = clib_host_to_net_u32 (tmp[0]);
  tmp = (u32 *) mp->last_ip_address;
  end_host_order = clib_host_to_net_u32 (tmp[0]);

  count = (end_host_order - start_host_order) + 1;

  if (count > 1024)
    clib_warning ("%U - %U, %d addresses...",
                  format_ip4_address, mp->first_ip_address,
                  format_ip4_address, mp->last_ip_address,
                  count);
  
  memcpy (&this_addr.as_u8, mp->first_ip_address, 4);

  for (i = 0; i < count; i++)
    {
      snat_add_address (sm, &this_addr);
      increment_v4_address (&this_addr);
    }

 send_reply:
  REPLY_MACRO (VL_API_SNAT_ADD_ADDRESS_RANGE_REPLY);
}

static void *vl_api_snat_add_address_range_t_print
(vl_api_snat_add_address_range_t *mp, void * handle)
{
  u8 * s;

  s = format (0, "SCRIPT: snat_add_address_range ");
  s = format (s, "%U ", format_ip4_address, mp->first_ip_address);
  if (memcmp (mp->first_ip_address, mp->last_ip_address, 4))
    {
      s = format (s, " - %U ", format_ip4_address, mp->last_ip_address);
    }
  FINISH;
}

static void
vl_api_snat_interface_add_del_feature_t_handler
(vl_api_snat_interface_add_del_feature_t * mp)
{
  snat_main_t * sm = &snat_main;
  vl_api_snat_interface_add_del_feature_reply_t * rmp;
  u8 is_del = mp->is_add == 0;
  u32 sw_if_index = ntohl(mp->sw_if_index);
  u32 ci;
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_config_main_t * rx_cm = &lm->feature_config_mains[VNET_IP_RX_UNICAST_FEAT];
  u32 feature_index;
  int rv = 0;

  VALIDATE_SW_IF_INDEX(mp);

  if (sm->static_mapping_only && !(sm->static_mapping_connection_tracking))
    feature_index = mp->is_inside ?  sm->rx_feature_in2out_fast
      : sm->rx_feature_out2in_fast;
  else
    feature_index = mp->is_inside ? sm->rx_feature_in2out
      : sm->rx_feature_out2in;

  ci = rx_cm->config_index_by_sw_if_index[sw_if_index];
  ci = (is_del
        ? vnet_config_del_feature
        : vnet_config_add_feature)
    (sm->vlib_main, &rx_cm->config_main,
     ci,
     feature_index,
     0 /* config struct */, 
     0 /* sizeof config struct*/);
  rx_cm->config_index_by_sw_if_index[sw_if_index] = ci;
  
  BAD_SW_IF_INDEX_LABEL;

  REPLY_MACRO(VL_API_SNAT_INTERFACE_ADD_DEL_FEATURE_REPLY);
}

static void *vl_api_snat_interface_add_del_feature_t_print
(vl_api_snat_interface_add_del_feature_t * mp, void *handle)
{
  u8 * s;

  s = format (0, "SCRIPT: snat_interface_add_del_feature ");
  s = format (s, "sw_if_index %d %s %s",
              clib_host_to_net_u32(mp->sw_if_index),
              mp->is_inside ? "in":"out",
              mp->is_add ? "" : "del");

  FINISH;
}

static void
vl_api_snat_add_static_mapping_t_handler
(vl_api_snat_add_static_mapping_t * mp)
{
  snat_main_t * sm = &snat_main;
  vl_api_snat_add_static_mapping_reply_t * rmp;
  ip4_address_t local_addr, external_addr;
  u16 local_port = 0, external_port = 0;
  u32 vrf_id;
  int rv = 0;

  if (mp->is_ip4 != 1)
    {
      rv = VNET_API_ERROR_UNIMPLEMENTED;
      goto send_reply;
    }

  memcpy (&local_addr.as_u8, mp->local_ip_address, 4);
  memcpy (&external_addr.as_u8, mp->external_ip_address, 4);
  if (mp->addr_only == 0)
    {
      local_port = clib_net_to_host_u16 (mp->local_port);
      external_port = clib_net_to_host_u16 (mp->external_port);
    }
  vrf_id = clib_net_to_host_u32 (mp->vrf_id);

  rv = snat_add_static_mapping(local_addr, external_addr, local_port,
                               external_port, vrf_id, mp->addr_only,
                               mp->is_add);

 send_reply:
  REPLY_MACRO (VL_API_SNAT_ADD_ADDRESS_RANGE_REPLY);
}

static void *vl_api_snat_add_static_mapping_t_print
(vl_api_snat_add_static_mapping_t *mp, void * handle)
{
  u8 * s;

  s = format (0, "SCRIPT: snat_add_static_mapping ");
  s = format (s, "local_addr %U external_addr %U ",
              format_ip4_address, mp->local_ip_address,
              format_ip4_address, mp->external_ip_address);

  if (mp->addr_only == 0)
    s = format (s, "local_port %d external_port %d ",
                clib_net_to_host_u16 (mp->local_port),
                clib_net_to_host_u16 (mp->external_port));

  if (mp->vrf_id != ~0)
    s = format (s, "vrf %d", clib_net_to_host_u32 (mp->vrf_id));

  FINISH;
}

static void
send_snat_static_mapping_details
(snat_static_mapping_t * m, unix_shared_memory_queue_t * q, u32 context)
{
  vl_api_snat_static_mapping_details_t *rmp;
  snat_main_t * sm = &snat_main;

  rmp = vl_msg_api_alloc (sizeof (*rmp));
  memset (rmp, 0, sizeof (*rmp));
  rmp->_vl_msg_id = ntohs (VL_API_SNAT_STATIC_MAPPING_DETAILS+sm->msg_id_base);
  rmp->is_ip4 = 1;
  rmp->addr_only = m->addr_only;
  clib_memcpy (rmp->local_ip_address, &(m->local_addr), 4);
  clib_memcpy (rmp->external_ip_address, &(m->external_addr), 4);
  rmp->local_port = htons (m->local_port);
  rmp->external_port = htons (m->external_port);
  rmp->vrf_id = htonl (m->vrf_id);
  rmp->context = context;

  vl_msg_api_send_shmem (q, (u8 *) & rmp);
}

static void
vl_api_snat_static_mapping_dump_t_handler
(vl_api_snat_static_mapping_dump_t * mp)
{
  unix_shared_memory_queue_t *q;
  snat_main_t * sm = &snat_main;
  snat_static_mapping_t * m;

  q = vl_api_client_index_to_input_queue (mp->client_index);
  if (q == 0)
    return;

  pool_foreach (m, sm->static_mappings,
  ({
      send_snat_static_mapping_details (m, q, mp->context);
  }));
}

static void *vl_api_snat_static_mapping_dump_t_print
(vl_api_snat_static_mapping_dump_t *mp, void * handle)
{
  u8 *s;

  s = format (0, "SCRIPT: snat_static_mapping_dump ");

  FINISH;
}

static void
vl_api_snat_control_ping_t_handler
(vl_api_snat_control_ping_t * mp)
{
  vl_api_snat_control_ping_reply_t *rmp;
  snat_main_t * sm = &snat_main;
  int rv = 0;

  REPLY_MACRO2(VL_API_SNAT_CONTROL_PING_REPLY,
  ({
    rmp->vpe_pid = ntohl (getpid());
  }));
}

static void *vl_api_snat_control_ping_t_print
(vl_api_snat_control_ping_t *mp, void * handle)
{
  u8 *s;

  s = format (0, "SCRIPT: snat_control_ping ");

  FINISH;
}

static void
vl_api_snat_show_config_t_handler
(vl_api_snat_show_config_t * mp)
{
  vl_api_snat_show_config_reply_t *rmp;
  snat_main_t * sm = &snat_main;
  int rv = 0;

  REPLY_MACRO2(VL_API_SNAT_SHOW_CONFIG_REPLY,
  ({
    rmp->translation_buckets = htons (sm->translation_buckets);
    rmp->translation_memory_size = htons (sm->translation_memory_size);
    rmp->user_buckets = htons (sm->user_buckets);
    rmp->user_memory_size = htons (sm->user_memory_size);
    rmp->max_translations_per_user = htons (sm->max_translations_per_user);
    rmp->outside_vrf_id = htons (sm->outside_vrf_id);
    rmp->inside_vrf_id = htons (sm->inside_vrf_id);
    rmp->static_mapping_only = sm->static_mapping_only;
    rmp->static_mapping_connection_tracking =
      sm->static_mapping_connection_tracking;
  }));
}

static void *vl_api_snat_show_config_t_print
(vl_api_snat_show_config_t *mp, void * handle)
{
  u8 *s;

  s = format (0, "SCRIPT: snat_show_config ");

  FINISH;
}

/* List of message types that this plugin understands */
#define foreach_snat_plugin_api_msg                                     \
_(SNAT_ADD_ADDRESS_RANGE, snat_add_address_range)                       \
_(SNAT_INTERFACE_ADD_DEL_FEATURE, snat_interface_add_del_feature)       \
_(SNAT_ADD_STATIC_MAPPING, snat_add_static_mapping)                     \
_(SNAT_CONTROL_PING, snat_control_ping)                                 \
_(SNAT_STATIC_MAPPING_DUMP, snat_static_mapping_dump)                   \
_(SNAT_SHOW_CONFIG, snat_show_config)

/* Set up the API message handling tables */
static clib_error_t *
snat_plugin_api_hookup (vlib_main_t *vm)
{
   snat_main_t * sm __attribute__ ((unused)) = &snat_main;
#define _(N,n)                                                  \
    vl_msg_api_set_handlers((VL_API_##N + sm->msg_id_base),     \
                           #n,					\
                           vl_api_##n##_t_handler,              \
                           vl_noop_handler,                     \
                           vl_api_##n##_t_endian,               \
                           vl_api_##n##_t_print,                \
                           sizeof(vl_api_##n##_t), 1); 
    foreach_snat_plugin_api_msg;
#undef _

    return 0;
}

static void plugin_custom_dump_configure (snat_main_t * sm) 
{
#define _(n,f) sm->api_main->msg_print_handlers \
  [VL_API_##n + sm->msg_id_base]                \
    = (void *) vl_api_##f##_t_print;
  foreach_snat_plugin_api_msg;
#undef _
}

static clib_error_t * snat_init (vlib_main_t * vm)
{
  snat_main_t * sm = &snat_main;
  clib_error_t * error = 0;
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  u8 * name;

  name = format (0, "snat_%08x%c", api_version, 0);

  /* Ask for a correctly-sized block of API message decode slots */
  sm->msg_id_base = vl_msg_api_get_msg_ids 
      ((char *) name, VL_MSG_FIRST_AVAILABLE);

  sm->vlib_main = vm;
  sm->vnet_main = vnet_get_main();
  sm->ip4_main = im;
  sm->ip4_lookup_main = lm;
  sm->api_main = &api_main;

  error = snat_plugin_api_hookup (vm);
  plugin_custom_dump_configure (sm);
  vec_free(name);

  return error;
}

VLIB_INIT_FUNCTION (snat_init);

void snat_free_outside_address_and_port (snat_main_t * sm, 
                                         snat_session_key_t * k, 
                                         u32 address_index)
{
  snat_address_t *a;
  u16 port_host_byte_order = clib_net_to_host_u16 (k->port);
  
  ASSERT (address_index < vec_len (sm->addresses));

  a = sm->addresses + address_index;

  ASSERT (clib_bitmap_get (a->busy_port_bitmap, port_host_byte_order) == 1);

  a->busy_port_bitmap = clib_bitmap_set (a->busy_port_bitmap, 
                                         port_host_byte_order, 0);
  a->busy_ports--;
}  

/**
 * @brief Match SNAT static mapping.
 *
 * @param sm          SNAT main.
 * @param match       Address and port to match.
 * @param mapping     External or local address and port of the matched mapping.
 * @param by_external If 0 match by local address otherwise match by external
 *                    address.
 *
 * @returns 0 if match found otherwise 1.
 */
int snat_static_mapping_match (snat_main_t * sm,
                               snat_session_key_t match,
                               snat_session_key_t * mapping,
                               u8 by_external)
{
  clib_bihash_kv_8_8_t kv, value;
  snat_static_mapping_t *m;
  snat_static_mapping_key_t m_key;
  clib_bihash_8_8_t *mapping_hash = &sm->static_mapping_by_local;

  if (by_external)
    mapping_hash = &sm->static_mapping_by_external;

  m_key.addr = match.addr;
  m_key.port = clib_net_to_host_u16 (match.port);
  m_key.pad = 0;

  kv.key = m_key.as_u64;

  if (clib_bihash_search_8_8 (mapping_hash, &kv, &value))
    {
      /* Try address only mapping */
      m_key.port = 0;
      kv.key = m_key.as_u64;
      if (clib_bihash_search_8_8 (mapping_hash, &kv, &value))
        return 1;
    }

  m = pool_elt_at_index (sm->static_mappings, value.value);

  if (by_external)
    {
      mapping->addr = m->local_addr;
      /* Address only mapping doesn't change port */
      mapping->port = m->addr_only ? match.port
        : clib_host_to_net_u16 (m->local_port);
      mapping->fib_index = m->fib_index;
    }
  else
    {
      mapping->addr = m->external_addr;
      /* Address only mapping doesn't change port */
      mapping->port = m->addr_only ? match.port
        : clib_host_to_net_u16 (m->external_port);
      mapping->fib_index = sm->outside_fib_index;
    }

  return 0;
}

int snat_alloc_outside_address_and_port (snat_main_t * sm, 
                                         snat_session_key_t * k,
                                         u32 * address_indexp)
{
  int i;
  snat_address_t *a;
  u32 portnum;

  for (i = 0; i < vec_len (sm->addresses); i++)
    {
      if (sm->addresses[i].busy_ports < (65535-1024))
        {
          a = sm->addresses + i;

          while (1)
            {
              portnum = random_u32 (&sm->random_seed);
              portnum &= 0xFFFF;
              if (portnum < 1024)
                continue;
              if (clib_bitmap_get (a->busy_port_bitmap, portnum))
                continue;
              a->busy_port_bitmap = clib_bitmap_set (a->busy_port_bitmap,
                                                     portnum, 1);
              a->busy_ports++;
              /* Caller sets protocol and fib index */
              k->addr = a->addr;
              k->port = clib_host_to_net_u16(portnum);
              *address_indexp = i;
              return 0;
            }
        }
    }
  /* Totally out of translations to use... */
  return 1;
}


static clib_error_t *
add_address_command_fn (vlib_main_t * vm,
                        unformat_input_t * input,
                        vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  snat_main_t * sm = &snat_main;
  ip4_address_t start_addr, end_addr, this_addr;
  u32 start_host_order, end_host_order;
  int i, count;

  /* Get a line of input. */
  if (!unformat_user (input, unformat_line_input, line_input))
    return 0;

  if (unformat (line_input, "%U - %U",
                unformat_ip4_address, &start_addr,
                unformat_ip4_address, &end_addr))
    ;
  else if (unformat (line_input, "%U", unformat_ip4_address, &start_addr))
    end_addr = start_addr;
  else
    return clib_error_return (0, "unknown input '%U'", format_unformat_error,
      input);
  unformat_free (line_input);

  if (sm->static_mapping_only)
    return clib_error_return (0, "static mapping only mode");

  start_host_order = clib_host_to_net_u32 (start_addr.as_u32);
  end_host_order = clib_host_to_net_u32 (end_addr.as_u32);
  
  if (end_host_order < start_host_order)
    return clib_error_return (0, "end address less than start address");

  count = (end_host_order - start_host_order) + 1;

  if (count > 1024)
    clib_warning ("%U - %U, %d addresses...",
                  format_ip4_address, &start_addr,
                  format_ip4_address, &end_addr,
                  count);
  
  this_addr = start_addr;

  for (i = 0; i < count; i++)
    {
      snat_add_address (sm, &this_addr);
      increment_v4_address (&this_addr);
    }

  return 0;
}

VLIB_CLI_COMMAND (add_address_command, static) = {
  .path = "snat add address",
  .short_help = "snat add addresses <ip4-range-start> [- <ip4-range-end>]",
  .function = add_address_command_fn,
};

static clib_error_t *
snat_feature_command_fn (vlib_main_t * vm,
                          unformat_input_t * input,
                          vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  vnet_main_t * vnm = vnet_get_main();
  snat_main_t * sm = &snat_main;
  ip4_main_t * im = &ip4_main;
  ip_lookup_main_t * lm = &im->lookup_main;
  ip_config_main_t * rx_cm = &lm->feature_config_mains[VNET_IP_RX_UNICAST_FEAT];
  clib_error_t * error = 0;
  u32 sw_if_index, ci;
  u32 feature_index;
  u32 * inside_sw_if_indices = 0;
  u32 * outside_sw_if_indices = 0;
  int is_del = 0;
  int i;

  sw_if_index = ~0;

  /* Get a line of input. */
  if (!unformat_user (input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "in %U", unformat_vnet_sw_interface,
                    vnm, &sw_if_index))
        vec_add1 (inside_sw_if_indices, sw_if_index);
      else if (unformat (line_input, "out %U", unformat_vnet_sw_interface,
                         vnm, &sw_if_index))
        vec_add1 (outside_sw_if_indices, sw_if_index);
      else if (unformat (line_input, "del"))
        is_del = 1;
      else
        return clib_error_return (0, "unknown input '%U'",
          format_unformat_error, input);
    }
  unformat_free (line_input);

  if (vec_len (inside_sw_if_indices))
    {
      if (sm->static_mapping_only && !(sm->static_mapping_connection_tracking))
        feature_index = sm->rx_feature_in2out_fast;
      else
        feature_index = sm->rx_feature_in2out;

      for (i = 0; i < vec_len(inside_sw_if_indices); i++)
        {
          sw_if_index = inside_sw_if_indices[i];
          ci = rx_cm->config_index_by_sw_if_index[sw_if_index];
          ci = (is_del
                ? vnet_config_del_feature
                : vnet_config_add_feature)
            (vm, &rx_cm->config_main,
             ci,
             feature_index,
             0 /* config struct */, 
             0 /* sizeof config struct*/);
          rx_cm->config_index_by_sw_if_index[sw_if_index] = ci;
        }
    }

  if (vec_len (outside_sw_if_indices))
    {
      if (sm->static_mapping_only && !(sm->static_mapping_connection_tracking))
        feature_index = sm->rx_feature_out2in_fast;
      else
        feature_index = sm->rx_feature_out2in;

      for (i = 0; i < vec_len(outside_sw_if_indices); i++)
        {
          sw_if_index = outside_sw_if_indices[i];
          ci = rx_cm->config_index_by_sw_if_index[sw_if_index];
          ci = (is_del
                ? vnet_config_del_feature
                : vnet_config_add_feature)
            (vm, &rx_cm->config_main,
             ci,
             feature_index,
             0 /* config struct */, 
             0 /* sizeof config struct*/);
          rx_cm->config_index_by_sw_if_index[sw_if_index] = ci;
        }
    }

  vec_free (inside_sw_if_indices);
  vec_free (outside_sw_if_indices);

  return error;
}

VLIB_CLI_COMMAND (set_interface_snat_command, static) = {
  .path = "set interface snat",
  .function = snat_feature_command_fn,
  .short_help = "set interface snat in <intfc> out <intfc> [del]",
};

static clib_error_t *
add_static_mapping_command_fn (vlib_main_t * vm,
                               unformat_input_t * input,
                               vlib_cli_command_t * cmd)
{
  unformat_input_t _line_input, *line_input = &_line_input;
  clib_error_t * error = 0;
  ip4_address_t l_addr, e_addr;
  u32 l_port = 0, e_port = 0, vrf_id = ~0;
  int is_add = 1;
  int addr_only = 1;
  int rv;

  /* Get a line of input. */
  if (!unformat_user (input, unformat_line_input, line_input))
    return 0;

  while (unformat_check_input (line_input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (line_input, "local %U %u", unformat_ip4_address, &l_addr,
                    &l_port))
        addr_only = 0;
      else if (unformat (line_input, "local %U", unformat_ip4_address, &l_addr))
        ;
      else if (unformat (line_input, "external %U %u", unformat_ip4_address,
                         &e_addr, &e_port))
        addr_only = 0;
      else if (unformat (line_input, "external %U", unformat_ip4_address,
                         &e_addr))
        ;
      else if (unformat (line_input, "vrf %u", &vrf_id))
        ;
      else if (unformat (line_input, "del"))
        is_add = 0;
      else
        return clib_error_return (0, "unknown input: '%U'",
          format_unformat_error, line_input);
    }
  unformat_free (line_input);

  rv = snat_add_static_mapping(l_addr, e_addr, (u16) l_port, (u16) e_port,
                               vrf_id, addr_only, is_add);

  switch (rv)
    {
    case VNET_API_ERROR_INVALID_VALUE:
      return clib_error_return (0, "External port already in use.");
      break;
    case VNET_API_ERROR_NO_SUCH_ENTRY:
      if (is_add)
        return clib_error_return (0, "External addres must be allocated.");
      else
        return clib_error_return (0, "Mapping not exist.");
      break;
    case VNET_API_ERROR_NO_SUCH_FIB:
      return clib_error_return (0, "No such VRF id.");
    case VNET_API_ERROR_VALUE_EXIST:
      return clib_error_return (0, "Mapping already exist.");
    default:
      break;
    }

  return error;
}

/*?
 * @cliexpar
 * @cliexstart{snat add static mapping}
 * Static mapping allows hosts on the external network to initiate connection
 * to to the local network host.
 * To create static mapping between local host address 10.0.0.3 port 6303 and
 * external address 4.4.4.4 port 3606 use:
 *  vpp# snat add static mapping local 10.0.0.3 6303 external 4.4.4.4 3606
 * If not runnig "static mapping only" S-NAT plugin mode use before:
 *  vpp# snat add address 4.4.4.4
 * To create static mapping between local and external address use:
 *  vpp# snat add static mapping local 10.0.0.3 external 4.4.4.4
 * @cliexend
?*/
VLIB_CLI_COMMAND (add_static_mapping_command, static) = {
  .path = "snat add static mapping",
  .function = add_static_mapping_command_fn,
  .short_help =
    "snat add static mapping local <addr> [<port>] external <addr> [<port>] [vrf <table-id>] [del]",
};

static clib_error_t *
snat_config (vlib_main_t * vm, unformat_input_t * input)
{
  snat_main_t * sm = &snat_main;
  u32 translation_buckets = 1024;
  u32 translation_memory_size = 128<<20;
  u32 user_buckets = 128;
  u32 user_memory_size = 64<<20;
  u32 max_translations_per_user = 100;
  u32 outside_vrf_id = 0;
  u32 inside_vrf_id = 0;
  u32 static_mapping_buckets = 1024;
  u32 static_mapping_memory_size = 64<<20;
  u8 static_mapping_only = 0;
  u8 static_mapping_connection_tracking = 0;

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "translation hash buckets %d", &translation_buckets))
        ;
      else if (unformat (input, "translation hash memory %d",
                         &translation_memory_size));
      else if (unformat (input, "user hash buckets %d", &user_buckets))
        ;
      else if (unformat (input, "user hash memory %d",
                         &user_memory_size))
        ;
      else if (unformat (input, "max translations per user %d",
                         &max_translations_per_user))
        ;
      else if (unformat (input, "outside VRF id %d",
                         &outside_vrf_id))
        ;
      else if (unformat (input, "inside VRF id %d",
                         &inside_vrf_id))
        ;
      else if (unformat (input, "static mapping only"))
        {
          static_mapping_only = 1;
          if (unformat (input, "connection tracking"))
            static_mapping_connection_tracking = 1;
        }
      else 
	return clib_error_return (0, "unknown input '%U'",
				  format_unformat_error, input);
    }

  /* for show commands, etc. */
  sm->translation_buckets = translation_buckets;
  sm->translation_memory_size = translation_memory_size;
  sm->user_buckets = user_buckets;
  sm->user_memory_size = user_memory_size;
  sm->max_translations_per_user = max_translations_per_user;
  sm->outside_vrf_id = outside_vrf_id;
  sm->outside_fib_index = ~0;
  sm->inside_vrf_id = inside_vrf_id;
  sm->inside_fib_index = ~0;
  sm->static_mapping_only = static_mapping_only;
  sm->static_mapping_connection_tracking = static_mapping_connection_tracking;

  if (!static_mapping_only ||
      (static_mapping_only && static_mapping_connection_tracking))
    {
      clib_bihash_init_8_8 (&sm->in2out, "in2out", translation_buckets,
                            translation_memory_size);

      clib_bihash_init_8_8 (&sm->out2in, "out2in", translation_buckets,
                            translation_memory_size);

      clib_bihash_init_8_8 (&sm->user_hash, "users", user_buckets,
                            user_memory_size);
    }
  clib_bihash_init_8_8 (&sm->static_mapping_by_local,
                        "static_mapping_by_local", static_mapping_buckets,
                        static_mapping_memory_size);

  clib_bihash_init_8_8 (&sm->static_mapping_by_external,
                        "static_mapping_by_external", static_mapping_buckets,
                        static_mapping_memory_size);
  return 0;
}

VLIB_CONFIG_FUNCTION (snat_config, "snat");

u8 * format_snat_key (u8 * s, va_list * args)
{
  snat_session_key_t * key = va_arg (*args, snat_session_key_t *);
  char * protocol_string = "unknown";
  static char *protocol_strings[] = {
      "UDP",
      "TCP",
      "ICMP",
  };

  if (key->protocol < ARRAY_LEN(protocol_strings))
      protocol_string = protocol_strings[key->protocol];

  s = format (s, "%U proto %s port %d fib %d",
              format_ip4_address, &key->addr, protocol_string,
              clib_net_to_host_u16 (key->port), key->fib_index);
  return s;
}

u8 * format_snat_session (u8 * s, va_list * args)
{
  snat_main_t * sm __attribute__((unused)) = va_arg (*args, snat_main_t *);
  snat_session_t * sess = va_arg (*args, snat_session_t *);

  s = format (s, "  i2o %U\n", format_snat_key, &sess->in2out);
  s = format (s, "    o2i %U\n", format_snat_key, &sess->out2in);
  s = format (s, "       last heard %.2f\n", sess->last_heard);
  s = format (s, "       total pkts %d, total bytes %lld\n",
              sess->total_pkts, sess->total_bytes);
  if (snat_is_session_static (sess))
    s = format (s, "       static translation\n");
  else
    s = format (s, "       dynamic translation\n");

  return s;
}

u8 * format_snat_user (u8 * s, va_list * args)
{
  snat_main_t * sm = va_arg (*args, snat_main_t *);
  snat_user_t * u = va_arg (*args, snat_user_t *);
  int verbose = va_arg (*args, int);
  dlist_elt_t * head, * elt;
  u32 elt_index, head_index;
  u32 session_index;
  snat_session_t * sess;

  s = format (s, "%U: %d dynamic translations, %d static translations\n",
              format_ip4_address, &u->addr, u->nsessions, u->nstaticsessions);

  if (verbose == 0)
    return s;

  if (u->nsessions || u->nstaticsessions)
    {
      head_index = u->sessions_per_user_list_head_index;
      head = pool_elt_at_index (sm->list_pool, head_index);

      elt_index = head->next;
      elt = pool_elt_at_index (sm->list_pool, elt_index);
      session_index = elt->value;

      while (session_index != ~0)
        {
          sess = pool_elt_at_index (sm->sessions, session_index);

          s = format (s, "  %U\n", format_snat_session, sm, sess);

          elt_index = elt->next;
          elt = pool_elt_at_index (sm->list_pool, elt_index);
          session_index = elt->value;
        }
    }

  return s;
}

u8 * format_snat_static_mapping (u8 * s, va_list * args)
{
  snat_static_mapping_t *m = va_arg (*args, snat_static_mapping_t *);

  if (m->addr_only)
      s = format (s, "local %U external %U vrf %d",
                  format_ip4_address, &m->local_addr,
                  format_ip4_address, &m->external_addr,
                  m->vrf_id);
  else
      s = format (s, "local %U:%d external %U:%d vrf %d",
                  format_ip4_address, &m->local_addr, m->local_port,
                  format_ip4_address, &m->external_addr, m->external_port,
                  m->vrf_id);

  return s;
}

static clib_error_t *
show_snat_command_fn (vlib_main_t * vm,
		 unformat_input_t * input,
		 vlib_cli_command_t * cmd)
{
  int verbose = 0;
  snat_main_t * sm = &snat_main;
  snat_user_t * u;
  snat_static_mapping_t *m;

  if (unformat (input, "detail"))
    verbose = 1;
  else if (unformat (input, "verbose"))
    verbose = 2;

  if (sm->static_mapping_only)
    {
      if (sm->static_mapping_connection_tracking)
        vlib_cli_output (vm, "SNAT mode: static mapping only connection "
                         "tracking");
      else
        vlib_cli_output (vm, "SNAT mode: static mapping only");
    }
  else
    {
      vlib_cli_output (vm, "SNAT mode: dynamic translations enabled");
    }

  if (sm->static_mapping_only && !(sm->static_mapping_connection_tracking))
    {
      vlib_cli_output (vm, "%d static mappings",
                       pool_elts (sm->static_mappings));

      if (verbose > 0)
        {
          pool_foreach (m, sm->static_mappings,
          ({
            vlib_cli_output (vm, "%U", format_snat_static_mapping, m);
          }));
        }
    }
  else
    {
      vlib_cli_output (vm, "%d users, %d outside addresses, %d active sessions,"
                       " %d static mappings",
                       pool_elts (sm->users),
                       vec_len (sm->addresses),
                       pool_elts (sm->sessions),
                       pool_elts (sm->static_mappings));

      if (verbose > 0)
        {
          vlib_cli_output (vm, "%U", format_bihash_8_8, &sm->in2out,
                           verbose - 1);
          vlib_cli_output (vm, "%U", format_bihash_8_8, &sm->out2in,
                           verbose - 1);
          vlib_cli_output (vm, "%d list pool elements",
                           pool_elts (sm->list_pool));

          pool_foreach (u, sm->users,
          ({
            vlib_cli_output (vm, "%U", format_snat_user, sm, u, verbose - 1);
          }));

          if (pool_elts (sm->static_mappings))
            {
              vlib_cli_output (vm, "static mappings:");
              pool_foreach (m, sm->static_mappings,
              ({
                vlib_cli_output (vm, "%U", format_snat_static_mapping, m);
              }));
            }
        }
    }

  return 0;
}

VLIB_CLI_COMMAND (show_snat_command, static) = {
    .path = "show snat",
    .short_help = "show snat",
    .function = show_snat_command_fn,
};
