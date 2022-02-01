
#include "gpu_shader_create_info.hh"

/* -------------------------------------------------------------------- */
/** \name Camera Velocity
 * \{ */

GPU_SHADER_CREATE_INFO(eevee_velocity_camera)
    .uniform_buf(0, "CameraData", "cam_prev")
    .uniform_buf(1, "CameraData", "cam_curr")
    .uniform_buf(2, "CameraData", "cam_next")
    .sampler(0, ImageType::DEPTH_2D, "depth_tx")
    .fragment_out(0, Type::VEC4, "out_velocity_camera")
    .fragment_out(1, Type::VEC4, "out_velocity_view")
    .typedef_source("eevee_shader_shared.hh")
    .fragment_source("eevee_velocity_camera_frag.glsl")
    .additional_info("draw_fullscreen");

/** \} */

/* -------------------------------------------------------------------- */
/** \name Surface Velocity
 * \{ */

GPU_SHADER_INTERFACE_INFO(eevee_velocity_surface_iface, "interp")
    .smooth(Type::VEC3, "P")
    .smooth(Type::VEC3, "P_next")
    .smooth(Type::VEC3, "P_prev");

GPU_SHADER_CREATE_INFO(eevee_velocity_surface_mesh)
    .uniform_buf(0, "CameraData", "cam_prev", Frequency::PASS)
    .uniform_buf(1, "CameraData", "cam_curr", Frequency::PASS)
    .uniform_buf(2, "CameraData", "cam_next", Frequency::PASS)
    .uniform_buf(3, "VelocityObjectData", "velocity", Frequency::BATCH)
    .vertex_in(0, Type::VEC3, "pos")
    .vertex_in(1, Type::VEC3, "prv")
    .vertex_in(2, Type::VEC3, "nxt")
    .fragment_out(0, Type::VEC4, "out_velocity_camera")
    .fragment_out(1, Type::VEC4, "out_velocity_view")
    .typedef_source("eevee_shader_shared.hh")
    .vertex_source("eevee_velocity_surface_mesh_vert.glsl")
    .fragment_source("eevee_velocity_surface_frag.glsl")
    .additional_info("draw_mesh");

/** \} */
