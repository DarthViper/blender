/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

/** \file
 * \ingroup edsculpt
 */

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_brush.h"
#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_image.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_pbvh.h"

#include "PIL_time_utildefines.h"

#include "BLI_math_color_blend.h"
#include "BLI_task.h"
#include "BLI_vector.hh"

#include "IMB_rasterizer.hh"

#include "WM_types.h"

#include "bmesh.h"

#include "ED_uvedit.h"

#include "sculpt_intern.h"
#include "sculpt_texture_paint_intern.hh"

namespace blender::ed::sculpt_paint::texture_paint {
namespace painting {
static void do_task_cb_ex(void *__restrict userdata,
                          const int n,
                          const TaskParallelTLS *__restrict tls)
{
  TexturePaintingUserData *data = static_cast<TexturePaintingUserData *>(userdata);
  Object *ob = data->ob;
  SculptSession *ss = ob->sculpt;
  ImBuf *drawing_target = ss->mode.texture_paint.drawing_target;
  const Brush *brush = data->brush;
  PBVHNode *node = data->nodes[n];
  NodeData *node_data = static_cast<NodeData *>(BKE_pbvh_node_texture_paint_data_get(node));
  BLI_assert(node_data != nullptr);

  const int thread_id = BLI_task_parallel_thread_id(tls);

  SculptBrushTest test;
  SculptBrushTestFn sculpt_brush_test_sq_fn = SCULPT_brush_test_init_with_falloff_shape(
      ss, &test, brush->falloff_shape);

  float3 brush_srgb(brush->rgb[0], brush->rgb[1], brush->rgb[2]);
  float4 brush_linear;
  srgb_to_linearrgb_v3_v3(brush_linear, brush_srgb);
  brush_linear[3] = 1.0f;
  MVert *mvert = SCULPT_mesh_deformed_mverts_get(ss);
  

  const float brush_strength = ss->cache->bstrength;

  for (int i = 0; i < node_data->pixels.size(); i++) {
    const float3 &local_pos = node_data->pixels.local_position(i, mvert);
    if (!sculpt_brush_test_sq_fn(&test, local_pos)) {
      continue;
    }

    float4 &color = node_data->pixels.color(i);
    const int2 &image_coord = node_data->pixels.image_coord(i);
    /* Although currently the pixel is loaded each time. I expect additional performance
     * improvement when moving the flushing to higher level on the callstack. */
    if (!node_data->pixels.is_dirty(i)) {
      int pixel_index = image_coord.y * drawing_target->x + image_coord.x;
      copy_v4_v4(color, &drawing_target->rect_float[pixel_index * 4]);
    }
    // const float falloff_strength = BKE_brush_curve_strength(brush, sqrtf(test.dist),
    // test.radius);
    const float3 normal(0.0f, 0.0f, 0.0f);
    const float3 face_normal(0.0f, 0.0f, 0.0f);
    const float mask = 0.0f;
    const float falloff_strength = SCULPT_brush_strength_factor(
        ss, brush, local_pos, sqrtf(test.dist), normal, face_normal, mask, 0, thread_id);

    blend_color_interpolate_float(color, color, brush_linear, falloff_strength * brush_strength);
    node_data->pixels.mark_dirty(i);
    BLI_rcti_do_minmax_v(&node_data->dirty_region, image_coord);
    node_data->flags.dirty = true;
  }
}
}  // namespace painting

struct ImageData {
  void *lock = nullptr;
  Image *image = nullptr;
  ImageUser *image_user = nullptr;
  ImBuf *image_buffer = nullptr;

  ~ImageData()
  {
    BKE_image_release_ibuf(image, image_buffer, lock);
  }

  static bool init_active_image(Object *ob, ImageData *r_image_data)
  {
    ED_object_get_active_image(
        ob, 1, &r_image_data->image, &r_image_data->image_user, nullptr, nullptr);
    if (r_image_data->image == nullptr) {
      return false;
    }
    r_image_data->image_buffer = BKE_image_acquire_ibuf(
        r_image_data->image, r_image_data->image_user, &r_image_data->lock);
    if (r_image_data->image_buffer == nullptr) {
      return false;
    }
    return true;
  }
};

extern "C" {
void SCULPT_do_texture_paint_brush(Sculpt *sd, Object *ob, PBVHNode **nodes, int totnode)
{
  SculptSession *ss = ob->sculpt;
  Brush *brush = BKE_paint_brush(&sd->paint);

  ImageData image_data;
  if (!ImageData::init_active_image(ob, &image_data)) {
    return;
  }

  ss->mode.texture_paint.drawing_target = image_data.image_buffer;

  TexturePaintingUserData data = {nullptr};
  data.ob = ob;
  data.brush = brush;
  data.nodes = nodes;

  TaskParallelSettings settings;
  BKE_pbvh_parallel_range_settings(&settings, true, totnode);

  TIMEIT_START(texture_painting);
  BLI_task_parallel_range(0, totnode, &data, painting::do_task_cb_ex, &settings);
  TIMEIT_END(texture_painting);

  ss->mode.texture_paint.drawing_target = nullptr;
}

void SCULPT_init_texture_paint(Object *ob)
{
  SculptSession *ss = ob->sculpt;
  ImageData image_data;
  if (!ImageData::init_active_image(ob, &image_data)) {
    return;
  }
  ss->mode.texture_paint.drawing_target = image_data.image_buffer;
  PBVHNode **nodes;
  int totnode;
  BKE_pbvh_search_gather(ss->pbvh, NULL, NULL, &nodes, &totnode);
  SCULPT_extract_pixels(ob, nodes, totnode);

  MEM_freeN(nodes);

  ss->mode.texture_paint.drawing_target = nullptr;
}

void SCULPT_flush_texture_paint(Object *ob)
{
  ImageData image_data;
  if (!ImageData::init_active_image(ob, &image_data)) {
    return;
  }

  SculptSession *ss = ob->sculpt;
  PBVHNode **nodes;
  int totnode;
  BKE_pbvh_search_gather(ss->pbvh, NULL, NULL, &nodes, &totnode);
  for (int n = 0; n < totnode; n++) {
    PBVHNode *node = nodes[n];
    NodeData *data = static_cast<NodeData *>(BKE_pbvh_node_texture_paint_data_get(node));
    if (data == nullptr) {
      continue;
    }

    if (data->flags.dirty) {
      data->flush(*image_data.image_buffer);
      data->mark_region(*image_data.image, *image_data.image_buffer);
    }
  }

  MEM_freeN(nodes);
}
}
}  // namespace blender::ed::sculpt_paint::texture_paint
