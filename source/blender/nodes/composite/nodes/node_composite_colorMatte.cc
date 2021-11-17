/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.hh"

/* ******************* Color Key ********************************************************** */
static bNodeSocketTemplate cmp_node_color_in[] = {
    {SOCK_RGBA, N_("Image"), 1.0f, 1.0f, 1.0f, 1.0f},
    {SOCK_RGBA, N_("Key Color"), 1.0f, 1.0f, 1.0f, 1.0f},
    {-1, ""},
};

static bNodeSocketTemplate cmp_node_color_out[] = {
    {SOCK_RGBA, N_("Image")},
    {SOCK_FLOAT, N_("Matte")},
    {-1, ""},
};

static void node_composit_init_color_matte(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeChroma *c = (NodeChroma *)MEM_callocN(sizeof(NodeChroma), "node color");
  node->storage = c;
  c->t1 = 0.01f;
  c->t2 = 0.1f;
  c->t3 = 0.1f;
  c->fsize = 0.0f;
  c->fstrength = 1.0f;
}

static int node_composite_gpu_color_matte(GPUMaterial *mat,
                                          bNode *node,
                                          bNodeExecData *UNUSED(execdata),
                                          GPUNodeStack *in,
                                          GPUNodeStack *out)
{
  const NodeChroma *data = (NodeChroma *)node->storage;

  /* Because the Hue wraps around. */
  const float hue_epsilon = data->t1 / 2.0f;

  return GPU_stack_link(mat,
                        node,
                        "node_composite_color_matte",
                        in,
                        out,
                        GPU_uniform(&hue_epsilon),
                        GPU_uniform(&data->t2),
                        GPU_uniform(&data->t3));
}

void register_node_type_cmp_color_matte(void)
{
  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_COLOR_MATTE, "Color Key", NODE_CLASS_MATTE, NODE_PREVIEW);
  node_type_socket_templates(&ntype, cmp_node_color_in, cmp_node_color_out);
  node_type_init(&ntype, node_composit_init_color_matte);
  node_type_storage(&ntype, "NodeChroma", node_free_standard_storage, node_copy_standard_storage);
  node_type_gpu(&ntype, node_composite_gpu_color_matte);

  nodeRegisterType(&ntype);
}
