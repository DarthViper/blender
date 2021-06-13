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
 */

#include "DNA_mesh_types.h"

#include "BKE_bvhutils.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "node_geometry_util.hh"

static bNodeSocketTemplate geo_node_raycast_in[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {SOCK_GEOMETRY, N_("Cast Geometry")},
    {SOCK_STRING, N_("Ray Direction")},
    {SOCK_VECTOR, N_("Ray Direction"), 1.0, 0.0, 0.0, 0.0, -FLT_MAX, FLT_MAX},
    {SOCK_STRING, N_("Ray Length")},
    {SOCK_FLOAT, N_("Ray Length"), 0.0, 0.0, 0.0, 0.0, 0.0f, FLT_MAX},
    {SOCK_STRING, N_("Hit")},
    {SOCK_STRING, N_("Hit Index")},
    {SOCK_STRING, N_("Hit Position")},
    {SOCK_STRING, N_("Hit Normal")},
    {SOCK_STRING, N_("Hit Distance")},
    {-1, ""},
};

static bNodeSocketTemplate geo_node_raycast_out[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {-1, ""},
};

static void geo_node_raycast_layout(uiLayout *layout, bContext *UNUSED(C), PointerRNA *ptr)
{
  uiItemR(layout, ptr, "domain", 0, IFACE_("Domain"), ICON_NONE);

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);
  uiItemR(layout, ptr, "input_type_ray_direction", 0, IFACE_("Ray Direction"), ICON_NONE);
  uiItemR(layout, ptr, "input_type_ray_length", 0, IFACE_("Ray Length"), ICON_NONE);
}

static void geo_node_raycast_init(bNodeTree *UNUSED(tree), bNode *node)
{
  NodeGeometryRaycast *data = (NodeGeometryRaycast *)MEM_callocN(sizeof(NodeGeometryRaycast),
                                                                 __func__);
  data->domain = ATTR_DOMAIN_AUTO;
  data->input_type_ray_direction = GEO_NODE_ATTRIBUTE_INPUT_ATTRIBUTE;
  data->input_type_ray_length = GEO_NODE_ATTRIBUTE_INPUT_FLOAT;
  node->storage = data;
}

static void geo_node_raycast_update(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeGeometryRaycast *node_storage = (NodeGeometryRaycast *)node->storage;
  blender::nodes::update_attribute_input_socket_availabilities(
      *node,
      "Ray Direction",
      (GeometryNodeAttributeInputMode)node_storage->input_type_ray_direction);
  blender::nodes::update_attribute_input_socket_availabilities(
      *node, "Ray Length", (GeometryNodeAttributeInputMode)node_storage->input_type_ray_length);
}

namespace blender::nodes {

static void raycast_to_mesh(const GeometrySet &src_geometry,
                            GeometryComponent &dst_component,
                            const VArray<float3> &ray_origins,
                            const VArray<float3> &ray_directions,
                            const VArray<float> &ray_lengths,
                            const MutableSpan<bool> r_hit,
                            const MutableSpan<int> r_hit_indices,
                            const MutableSpan<float3> r_hit_positions,
                            const MutableSpan<float3> r_hit_normals,
                            const MutableSpan<float> r_hit_distances)
{
  BLI_assert(ray_origins.size() == ray_directions.size());
  BLI_assert(ray_origins.size() == ray_lengths.size());
  BLI_assert(ray_origins.size() == r_hit.size() || r_hit.is_empty());
  BLI_assert(ray_origins.size() == r_hit_indices.size() || r_hit_indices.is_empty());
  BLI_assert(ray_origins.size() == r_hit_positions.size() || r_hit_positions.is_empty());
  BLI_assert(ray_origins.size() == r_hit_normals.size() || r_hit_normals.is_empty());
  BLI_assert(ray_origins.size() == r_hit_distances.size() || r_hit_distances.is_empty());

  const MeshComponent *component = src_geometry.get_component_for_read<MeshComponent>();
  if (component == nullptr) {
    return;
  }
  const Mesh *mesh = component->get_for_read();
  if (mesh == nullptr) {
    return;
  }
  if (mesh->totpoly == 0) {
    return;
  }

  BVHTreeFromMesh tree_data;
  BKE_bvhtree_from_mesh_get(&tree_data, const_cast<Mesh *>(mesh), BVHTREE_FROM_LOOPTRI, 4);

  if (tree_data.tree != NULL) {
    for (const int i : ray_origins.index_range()) {
      const float ray_length = ray_lengths[i];
      const float3 ray_origin = ray_origins[i];
      const float3 ray_direction = ray_directions[i].normalized();

      BVHTreeRayHit hit;
      hit.index = -1;
      hit.dist = ray_length;
      if (BLI_bvhtree_ray_cast(tree_data.tree,
                               ray_origin,
                               ray_direction,
                               0.0f,
                               &hit,
                               tree_data.raycast_callback,
                               &tree_data) != -1) {
        if (!r_hit.is_empty()) {
          r_hit[i] = hit.index >= 0;
        }
        if (!r_hit_indices.is_empty()) {
          r_hit_indices[i] = hit.index;
        }
        if (!r_hit_positions.is_empty()) {
          r_hit_positions[i] = hit.co;
        }
        if (!r_hit_normals.is_empty()) {
          r_hit_normals[i] = hit.no;
        }
        if (!r_hit_distances.is_empty()) {
          r_hit_distances[i] = hit.dist;
        }
      }
      else {
        if (!r_hit.is_empty()) {
          r_hit[i] = false;
        }
        if (!r_hit_indices.is_empty()) {
          r_hit_indices[i] = -1;
        }
        if (!r_hit_positions.is_empty()) {
          r_hit_positions[i] = float3(0.0f, 0.0f, 0.0f);
        }
        if (!r_hit_normals.is_empty()) {
          r_hit_normals[i] = float3(0.0f, 0.0f, 0.0f);
        }
        if (!r_hit_distances.is_empty()) {
          r_hit_distances[i] = ray_length;
        }
      }
    }

    free_bvhtree_from_mesh(&tree_data);
  }
}

template<class ComponentT>
static void try_append_attribute_meta_data(const GeometrySet &geometry,
                                           const StringRef attribute_name,
                                           Vector<CustomDataType> &data_types,
                                           Vector<AttributeDomain> &domains)
{
  const ComponentT *component = geometry.get_component_for_read<ComponentT>();
  if (component != nullptr) {
    std::optional<AttributeMetaData> meta_data = component->attribute_get_meta_data(
        attribute_name);
    if (meta_data.has_value()) {
      data_types.append(meta_data->data_type);
      domains.append(meta_data->domain);
    }
  }
}

static void get_result_domain_and_data_type(const GeometrySet &geometry,
                                            const GeometryComponent &component,
                                            const StringRef attribute_name,
                                            CustomDataType &r_data_type,
                                            AttributeDomain &r_domain)
{
  Vector<CustomDataType> data_types;
  Vector<AttributeDomain> domains;

  try_append_attribute_meta_data<PointCloudComponent>(
      geometry, attribute_name, data_types, domains);
  try_append_attribute_meta_data<MeshComponent>(geometry, attribute_name, data_types, domains);

  r_data_type = bke::attribute_data_type_highest_complexity(data_types);

  /* TODO would be nice to have a function that knows about supported domain types, like:
   *   bke::attribute_supported_domain_highest_priority(component, domains);
   */
  if (component.type() == GEO_COMPONENT_TYPE_POINT_CLOUD) {
    r_domain = ATTR_DOMAIN_POINT;
  }
  else {
    r_domain = bke::attribute_domain_highest_priority(domains);
  }
}

static void raycast_from_points(const GeoNodeExecParams &params,
                                const GeometrySet &src_geometry,
                                GeometryComponent &dst_component,
                                const StringRef hit_name,
                                const StringRef hit_index_name,
                                const StringRef hit_position_name,
                                const StringRef hit_normal_name,
                                const StringRef hit_distance_name)
{
  const NodeGeometryRaycast &storage = *(const NodeGeometryRaycast *)params.node().storage;
  const AttributeDomain domain = (AttributeDomain)storage.domain;

  CustomDataType data_type;
  AttributeDomain auto_domain;
  get_result_domain_and_data_type(
      src_geometry, dst_component, "position", data_type, auto_domain);
  const AttributeDomain result_domain = (domain == ATTR_DOMAIN_AUTO) ? auto_domain : domain;

  GVArray_Typed<float3> ray_origins = dst_component.attribute_get_for_read<float3>(
      "position", result_domain, {0, 0, 0});
  GVArray_Typed<float3> ray_directions = params.get_input_attribute<float3>(
      "Ray Direction", dst_component, result_domain, {0, 0, 0});
  GVArray_Typed<float> ray_lengths = params.get_input_attribute<float>(
      "Ray Length", dst_component, result_domain, 0);

  OutputAttribute_Typed<bool> hit_attribute =
      dst_component.attribute_try_get_for_output_only<bool>(hit_name, result_domain);
  OutputAttribute_Typed<int> hit_index_attribute =
      dst_component.attribute_try_get_for_output_only<int>(hit_index_name, result_domain);
  OutputAttribute_Typed<float3> hit_position_attribute =
      dst_component.attribute_try_get_for_output_only<float3>(hit_position_name, result_domain);
  OutputAttribute_Typed<float3> hit_normal_attribute =
      dst_component.attribute_try_get_for_output_only<float3>(hit_normal_name, result_domain);
  OutputAttribute_Typed<float> hit_distance_attribute =
      dst_component.attribute_try_get_for_output_only<float>(hit_distance_name, result_domain);
  const MutableSpan<bool> hit_span = hit_attribute ? hit_attribute.as_span() : MutableSpan<bool>();
  const MutableSpan<int> hit_index_span = hit_index_attribute ? hit_index_attribute.as_span() :
                                                                MutableSpan<int>();
  const MutableSpan<float3> hit_position_span = hit_position_attribute ?
                                                    hit_position_attribute.as_span() :
                                                    MutableSpan<float3>();
  const MutableSpan<float3> hit_normal_span = hit_normal_attribute ?
                                                  hit_normal_attribute.as_span() :
                                                  MutableSpan<float3>();
  const MutableSpan<float> hit_distance_span = hit_distance_attribute ?
                                                   hit_distance_attribute.as_span() :
                                                   MutableSpan<float>();

  raycast_to_mesh(src_geometry,
                  dst_component,
                  ray_origins,
                  ray_directions,
                  ray_lengths,
                  hit_span,
                  hit_index_span,
                  hit_position_span,
                  hit_normal_span,
                  hit_distance_span);
}

static void geo_node_raycast_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  GeometrySet cast_geometry_set = params.extract_input<GeometrySet>("Cast Geometry");

  const std::string hit_name = params.extract_input<std::string>("Hit");
  const std::string hit_index_name = params.extract_input<std::string>("Hit Index");
  const std::string hit_position_name = params.extract_input<std::string>("Hit Position");
  const std::string hit_normal_name = params.extract_input<std::string>("Hit Normal");
  const std::string hit_distance_name = params.extract_input<std::string>("Hit Distance");

  geometry_set = bke::geometry_set_realize_instances(geometry_set);
  cast_geometry_set = bke::geometry_set_realize_instances(cast_geometry_set);

  if (geometry_set.has<MeshComponent>()) {
    raycast_from_points(params,
                        cast_geometry_set,
                        geometry_set.get_component_for_write<MeshComponent>(),
                        hit_name,
                        hit_index_name,
                        hit_position_name,
                        hit_normal_name,
                        hit_distance_name);
  }
  if (geometry_set.has<PointCloudComponent>()) {
    raycast_from_points(params,
                        cast_geometry_set,
                        geometry_set.get_component_for_write<MeshComponent>(),
                        hit_name,
                        hit_index_name,
                        hit_position_name,
                        hit_normal_name,
                        hit_distance_name);
  }

  params.set_output("Geometry", geometry_set);
}

}  // namespace blender::nodes

void register_node_type_geo_raycast()
{
  static bNodeType ntype;

  geo_node_type_base(&ntype, GEO_NODE_RAYCAST, "Raycast", NODE_CLASS_GEOMETRY, 0);
  node_type_socket_templates(&ntype, geo_node_raycast_in, geo_node_raycast_out);
  node_type_init(&ntype, geo_node_raycast_init);
  node_type_update(&ntype, geo_node_raycast_update);
  node_type_storage(
      &ntype, "NodeGeometryRaycast", node_free_standard_storage, node_copy_standard_storage);
  ntype.geometry_node_execute = blender::nodes::geo_node_raycast_exec;
  ntype.draw_buttons = geo_node_raycast_layout;
  nodeRegisterType(&ntype);
}
