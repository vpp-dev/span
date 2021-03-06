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
/**
 * @file
 * @brief Common utility functions for IPv4, IPv6 and L2 LISP-GPE adjacencys.
 *
 */

#include <vnet/dpo/dpo.h>
#include <vnet/lisp-gpe/lisp_gpe_sub_interface.h>
#include <vnet/lisp-gpe/lisp_gpe_adjacency.h>
#include <vnet/lisp-gpe/lisp_gpe_tunnel.h>
#include <vnet/fib/fib_entry.h>
#include <vnet/adj/adj_midchain.h>

/**
 * Memory pool of all adjacencies
 */
static lisp_gpe_adjacency_t *lisp_adj_pool;

/**
 * Hash table of all adjacencies. key:{nh, itf}
 * We never have an all zeros address since the interfaces are multi-access,
 * therefore there is no ambiguity between a v4 and v6 next-hop, so we don't
 * need to add the protocol to the key.
 */
static
BVT (clib_bihash)
  lisp_adj_db;

#define LISP_ADJ_SET_KEY(_key, _itf, _nh)       \
{						\
  _key.key[0] = (_nh)->ip.v6.as_u64[0];		\
  _key.key[1] = (_nh)->ip.v6.as_u64[1];		\
  _key.key[2] = (_itf);				\
}

     static index_t lisp_adj_find (const ip_address_t * addr, u32 sw_if_index)
{
  BVT (clib_bihash_kv) kv;

  LISP_ADJ_SET_KEY (kv, sw_if_index, addr);

  if (BV (clib_bihash_search) (&lisp_adj_db, &kv, &kv) < 0)
    {
      return (INDEX_INVALID);
    }
  else
    {
      return (kv.value);
    }
}

static void
lisp_adj_insert (const ip_address_t * addr, u32 sw_if_index, index_t ai)
{
  BVT (clib_bihash_kv) kv;

  LISP_ADJ_SET_KEY (kv, sw_if_index, addr);
  kv.value = ai;

  BV (clib_bihash_add_del) (&lisp_adj_db, &kv, 1);
}

static void
lisp_adj_remove (const ip_address_t * addr, u32 sw_if_index)
{
  BVT (clib_bihash_kv) kv;

  LISP_ADJ_SET_KEY (kv, sw_if_index, addr);

  BV (clib_bihash_add_del) (&lisp_adj_db, &kv, 0);
}

static lisp_gpe_adjacency_t *
lisp_gpe_adjacency_get_i (index_t lai)
{
  return (pool_elt_at_index (lisp_adj_pool, lai));
}

fib_forward_chain_type_t
lisp_gpe_adj_get_fib_chain_type (const lisp_gpe_adjacency_t * ladj)
{
  switch (ip_addr_version (&ladj->remote_rloc))
    {
    case IP4:
      return (FIB_FORW_CHAIN_TYPE_UNICAST_IP4);
    case IP6:
      return (FIB_FORW_CHAIN_TYPE_UNICAST_IP6);
    default:
      ASSERT (0);
      break;
    }
  return (FIB_FORW_CHAIN_TYPE_UNICAST_IP4);
}

/**
 * @brief Stack the tunnel's midchain on the IP forwarding chain of the via
 */
static void
lisp_gpe_adj_stack (lisp_gpe_adjacency_t * ladj)
{
  const lisp_gpe_tunnel_t *lgt;
  dpo_id_t tmp = DPO_NULL;
  fib_link_t linkt;

  lgt = lisp_gpe_tunnel_get (ladj->tunnel_index);
  fib_entry_contribute_forwarding (lgt->fib_entry_index,
				   lisp_gpe_adj_get_fib_chain_type (ladj),
				   &tmp);

  FOR_EACH_FIB_LINK (linkt)
  {
    if (FIB_LINK_MPLS == linkt)
      continue;
    adj_nbr_midchain_stack (ladj->adjs[linkt], &tmp);
  }
  dpo_reset (&tmp);
}

static lisp_gpe_next_protocol_e
lisp_gpe_adj_proto_from_fib_link_type (fib_link_t linkt)
{
  switch (linkt)
    {
    case FIB_LINK_IP4:
      return (LISP_GPE_NEXT_PROTO_IP4);
    case FIB_LINK_IP6:
      return (LISP_GPE_NEXT_PROTO_IP6);
    case FIB_LINK_ETHERNET:
      return (LISP_GPE_NEXT_PROTO_ETHERNET);
    default:
      ASSERT (0);
    }
  return (LISP_GPE_NEXT_PROTO_IP4);
}

#define is_v4_packet(_h) ((*(u8*) _h) & 0xF0) == 0x40

static void
lisp_gpe_fixup (vlib_main_t * vm, ip_adjacency_t * adj, vlib_buffer_t * b)
{
  /* Fixup the checksum and len fields in the LISP tunnel encap
   * that was applied at the midchain node */
  ip_udp_fixup_one (vm, b, is_v4_packet (vlib_buffer_get_current (b)));
}

index_t
lisp_gpe_adjacency_find_or_create_and_lock (const locator_pair_t * pair,
					    u32 overlay_table_id, u32 vni)
{
  const lisp_gpe_tunnel_t *lgt;
  lisp_gpe_adjacency_t *ladj;
  index_t lai, l3si;

  /*
   * first find the L3 sub-interface that corresponds to the loacl-rloc and vni
   */
  l3si = lisp_gpe_sub_interface_find_or_create_and_lock (&pair->lcl_loc,
							 overlay_table_id,
							 vni);

  /*
   * find an existing or create a new adj
   */
  lai = lisp_adj_find (&pair->rmt_loc, l3si);

  if (INDEX_INVALID == lai)
    {
      const lisp_gpe_sub_interface_t *l3s;
      u8 *rewrite = NULL;
      fib_link_t linkt;
      fib_prefix_t nh;

      pool_get (lisp_adj_pool, ladj);
      memset (ladj, 0, sizeof (*ladj));
      lai = (ladj - lisp_adj_pool);

      ladj->remote_rloc = pair->rmt_loc;
      ladj->vni = vni;
      /* transfer the lock to the adj */
      ladj->lisp_l3_sub_index = l3si;

      l3s = lisp_gpe_sub_interface_get (l3si);
      ladj->sw_if_index = l3s->sw_if_index;

      /* if vni is non-default */
      if (ladj->vni)
	ladj->flags = LISP_GPE_FLAGS_I;

      /* work in lisp-gpe not legacy mode */
      ladj->flags |= LISP_GPE_FLAGS_P;

      /*
       * find the tunnel that will provide the underlying transport
       * and hence the rewrite.
       * The RLOC FIB index is default table - always.
       */
      ladj->tunnel_index = lisp_gpe_tunnel_find_or_create_and_lock (pair, 0);

      lgt = lisp_gpe_tunnel_get (ladj->tunnel_index);

      /*
       * become of child of the RLOC FIB entry so we are updated when
       * its reachability changes, allowing us to re-stack the midcahins
       */
      ladj->fib_entry_child_index = fib_entry_child_add (lgt->fib_entry_index,
							 FIB_NODE_TYPE_LISP_ADJ,
							 lai);
      ip_address_to_fib_prefix (&pair->rmt_loc, &nh);

      /*
       * construct and stack the FIB midchain adjacencies
       */
      FOR_EACH_FIB_LINK (linkt)
      {
	if (FIB_LINK_MPLS == linkt)
	  continue;

	ladj->adjs[linkt] = adj_nbr_add_or_lock (nh.fp_proto,
						 linkt,
						 &nh.fp_addr,
						 ladj->sw_if_index);

	rewrite =
	  lisp_gpe_tunnel_build_rewrite (lgt, ladj,
					 lisp_gpe_adj_proto_from_fib_link_type
					 (linkt));

	adj_nbr_midchain_update_rewrite (ladj->adjs[linkt],
					 lisp_gpe_fixup,
					 (FIB_LINK_ETHERNET == linkt ?
					  ADJ_MIDCHAIN_FLAG_NO_COUNT :
					  ADJ_MIDCHAIN_FLAG_NONE), rewrite);

	vec_free (rewrite);
      }

      lisp_gpe_adj_stack (ladj);

      lisp_adj_insert (&ladj->remote_rloc, ladj->lisp_l3_sub_index, lai);
    }
  else
    {
      /* unlock the interface from the find. */
      lisp_gpe_sub_interface_unlock (l3si);
      ladj = lisp_gpe_adjacency_get_i (lai);
    }

  ladj->locks++;

  return (lai);
}

/**
 * @brief Get a pointer to a tunnel from a pointer to a FIB node
 */
static lisp_gpe_adjacency_t *
lisp_gpe_adjacency_from_fib_node (const fib_node_t * node)
{
  return ((lisp_gpe_adjacency_t *)
	  ((char *) node -
	   STRUCT_OFFSET_OF (lisp_gpe_adjacency_t, fib_node)));
}

static void
lisp_gpe_adjacency_last_lock_gone (lisp_gpe_adjacency_t * ladj)
{
  /*
   * no children so we are not counting locks. no-op.
   * at least not counting
   */
  lisp_adj_remove (&ladj->remote_rloc, ladj->lisp_l3_sub_index);

  /*
   * unlock the resources this adj holds
   */
  lisp_gpe_tunnel_unlock (ladj->tunnel_index);
  lisp_gpe_sub_interface_unlock (ladj->lisp_l3_sub_index);

  pool_put (lisp_adj_pool, ladj);
}

void
lisp_gpe_adjacency_unlock (index_t lai)
{
  lisp_gpe_adjacency_t *ladj;

  ladj = lisp_gpe_adjacency_get_i (lai);

  ladj->locks--;

  if (0 == ladj->locks)
    {
      lisp_gpe_adjacency_last_lock_gone (ladj);
    }
}

const lisp_gpe_adjacency_t *
lisp_gpe_adjacency_get (index_t lai)
{
  return (lisp_gpe_adjacency_get_i (lai));
}


/**
 * @brief LISP GPE tunnel back walk
 *
 * The FIB entry through which this tunnel resolves has been updated.
 * re-stack the midchain on the new forwarding.
 */
static fib_node_back_walk_rc_t
lisp_gpe_adjacency_back_walk (fib_node_t * node,
			      fib_node_back_walk_ctx_t * ctx)
{
  lisp_gpe_adj_stack (lisp_gpe_adjacency_from_fib_node (node));

  return (FIB_NODE_BACK_WALK_CONTINUE);
}

static fib_node_t *
lisp_gpe_adjacency_get_fib_node (fib_node_index_t index)
{
  lisp_gpe_adjacency_t *ladj;

  ladj = pool_elt_at_index (lisp_adj_pool, index);
  return (&ladj->fib_node);
}

static void
lisp_gpe_adjacency_last_fib_lock_gone (fib_node_t * node)
{
  lisp_gpe_adjacency_last_lock_gone (lisp_gpe_adjacency_from_fib_node (node));
}

const static fib_node_vft_t lisp_gpe_tuennel_vft = {
  .fnv_get = lisp_gpe_adjacency_get_fib_node,
  .fnv_back_walk = lisp_gpe_adjacency_back_walk,
  .fnv_last_lock = lisp_gpe_adjacency_last_fib_lock_gone,
};

u8 *
format_lisp_gpe_adjacency (u8 * s, va_list * args)
{
  lisp_gpe_adjacency_t *ladj = va_arg (*args, lisp_gpe_adjacency_t *);
  lisp_gpe_adjacency_format_flags_t flags =
    va_arg (args, lisp_gpe_adjacency_format_flags_t);

  if (flags & LISP_GPE_ADJ_FORMAT_FLAG_DETAIL)
    {
      s =
	format (s, "index %d locks:%d\n", ladj - lisp_adj_pool, ladj->locks);
    }

  s = format (s, " vni: %d,", ladj->vni);
  s = format (s, " remote-RLOC: %U,", format_ip_address, &ladj->remote_rloc);

  if (flags & LISP_GPE_ADJ_FORMAT_FLAG_DETAIL)
    {
      s = format (s, " %U\n",
		  format_lisp_gpe_sub_interface,
		  lisp_gpe_sub_interface_get (ladj->lisp_l3_sub_index));
      s = format (s, " %U\n",
		  format_lisp_gpe_tunnel,
		  lisp_gpe_tunnel_get (ladj->tunnel_index));
      s = format (s, " FIB adjacencies: IPV4:%d IPv6:%d L2:%d\n",
		  ladj->adjs[FIB_LINK_IP4],
		  ladj->adjs[FIB_LINK_IP6], ladj->adjs[FIB_LINK_ETHERNET]);
    }
  else
    {
      s = format (s, " LISP L3 sub-interface index: %d,",
		  ladj->lisp_l3_sub_index);
      s = format (s, " LISP tunnel index: %d", ladj->tunnel_index);
    }


  return (s);
}

static clib_error_t *
lisp_gpe_adjacency_show (vlib_main_t * vm,
			 unformat_input_t * input, vlib_cli_command_t * cmd)
{
  lisp_gpe_adjacency_t *ladj;
  index_t index;

  if (pool_elts (lisp_adj_pool) == 0)
    vlib_cli_output (vm, "No lisp-gpe Adjacencies");

  if (unformat (input, "%d", &index))
    {
      ladj = lisp_gpe_adjacency_get_i (index);
      vlib_cli_output (vm, "%U", format_lisp_gpe_adjacency, ladj,
		       LISP_GPE_ADJ_FORMAT_FLAG_DETAIL);
    }
  else
    {
      /* *INDENT-OFF* */
      pool_foreach (ladj, lisp_adj_pool,
      ({
	vlib_cli_output (vm, "[%d] %U\n",
			 ladj - lisp_adj_pool,
			 format_lisp_gpe_adjacency, ladj,
			 LISP_GPE_ADJ_FORMAT_FLAG_NONE);
      }));
      /* *INDENT-ON* */
    }

  return 0;
}

/* *INDENT-OFF* */
VLIB_CLI_COMMAND (show_lisp_gpe_tunnel_command, static) =
{
  .path = "show lisp gpe adjacency",
  .function = lisp_gpe_adjacency_show,
};
/* *INDENT-ON* */

#define LISP_ADJ_NBR_DEFAULT_HASH_NUM_BUCKETS (256)
#define LISP_ADJ_NBR_DEFAULT_HASH_MEMORY_SIZE (1<<20)

static clib_error_t *
lisp_gpe_adj_module_init (vlib_main_t * vm)
{
  BV (clib_bihash_init) (&lisp_adj_db,
			 "Adjacency Neighbour table",
			 LISP_ADJ_NBR_DEFAULT_HASH_NUM_BUCKETS,
			 LISP_ADJ_NBR_DEFAULT_HASH_MEMORY_SIZE);

  fib_node_register_type (FIB_NODE_TYPE_LISP_ADJ, &lisp_gpe_tuennel_vft);
  return (NULL);
}

VLIB_INIT_FUNCTION (lisp_gpe_adj_module_init)
/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
