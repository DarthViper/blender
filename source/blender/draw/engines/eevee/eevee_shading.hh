/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 Blender Foundation.
 */

/** \file
 * \ingroup eevee
 *
 * Shading passes contain drawcalls specific to shading pipelines.
 * They are to be shared across views.
 * This file is only for shading passes. Other passes are declared in their own module.
 */

#pragma once

#include "DRW_render.h"

#include "eevee_lut.h"

#include "eevee_gbuffer.hh"
#include "eevee_raytracing.hh"
#include "eevee_shadow.hh"
#include "eevee_velocity.hh"

namespace blender::eevee {

class Instance;

/* -------------------------------------------------------------------- */
/** \name Background Pass
 *
 * Render world values.
 * \{ */

class BackgroundPass {
 private:
  Instance &inst_;

  DRWPass *background_ps_ = nullptr;

 public:
  BackgroundPass(Instance &inst) : inst_(inst){};

  void sync(GPUMaterial *gpumat, GPUTexture *loodev_tx = nullptr);
  void render(void);
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Forward Pass
 *
 * Handles alpha blended surfaces and NPR materials (using Closure to RGBA).
 * \{ */

class ForwardPass {
 private:
  Instance &inst_;

  DRWPass *prepass_ps_ = nullptr;
  DRWPass *prepass_culled_ps_ = nullptr;
  DRWPass *opaque_ps_ = nullptr;
  DRWPass *opaque_culled_ps_ = nullptr;
  DRWPass *transparent_ps_ = nullptr;

  GPUTexture *input_screen_radiance_tx_ = nullptr;

 public:
  ForwardPass(Instance &inst) : inst_(inst){};

  void sync(void);

  DRWShadingGroup *material_add(::Material *blender_mat, GPUMaterial *gpumat)
  {
    return (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSPARENT)) ?
               material_transparent_add(blender_mat, gpumat) :
               material_opaque_add(blender_mat, gpumat);
  }

  DRWShadingGroup *prepass_add(::Material *blender_mat, GPUMaterial *gpumat)
  {
    return (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSPARENT)) ?
               prepass_transparent_add(blender_mat, gpumat) :
               prepass_opaque_add(blender_mat, gpumat);
  }

  DRWShadingGroup *material_opaque_add(::Material *blender_mat, GPUMaterial *gpumat);
  DRWShadingGroup *prepass_opaque_add(::Material *blender_mat, GPUMaterial *gpumat);
  DRWShadingGroup *material_transparent_add(::Material *blender_mat, GPUMaterial *gpumat);
  DRWShadingGroup *prepass_transparent_add(::Material *blender_mat, GPUMaterial *gpumat);

  void render(const DRWView *view, GPUTexture *depth_tx, GPUTexture *combined_tx);
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Deferred lighting.
 * \{ */

class DeferredLayer {
 private:
  Instance &inst_;

  DRWPass *prepass_ps_ = nullptr;
  DRWPass *prepass_culled_ps_ = nullptr;
  DRWPass *gbuffer_ps_ = nullptr;
  DRWPass *gbuffer_culled_ps_ = nullptr;
  DRWPass *volume_ps_ = nullptr;

  /* Closures bits from the materials in this pass. */
  eClosureBits closure_bits_;

 public:
  DeferredLayer(Instance &inst) : inst_(inst){};

  void sync(void);
  DRWShadingGroup *material_add(::Material *blender_mat, GPUMaterial *gpumat);
  DRWShadingGroup *prepass_add(::Material *blender_mat, GPUMaterial *gpumat);
  void volume_add(Object *ob);
  void render(const DRWView *view,
              RaytraceBuffer *rt_buffer,
              GPUTexture *depth_tx,
              GPUTexture *combined_tx);

 private:
  void deferred_shgroup_resources(DRWShadingGroup *grp);
};

class DeferredPass {
  friend DeferredLayer;

 private:
  Instance &inst_;

  /* Gbuffer filling passes. We could have an arbitrary number of them but for now we just have
   * a hardcoded number of them. */
  DeferredLayer opaque_layer_;
  DeferredLayer refraction_layer_;
  DeferredLayer volumetric_layer_;

  DRWPass *eval_ps_ = nullptr;
  DRWPass *eval_subsurface_ps_ = nullptr;

  /* References only. */
  GPUTexture *input_combined_tx_ = nullptr;

 public:
  DeferredPass(Instance &inst)
      : inst_(inst), opaque_layer_(inst), refraction_layer_(inst), volumetric_layer_(inst){};

  void sync(void);
  DRWShadingGroup *material_add(::Material *material, GPUMaterial *gpumat);
  DRWShadingGroup *prepass_add(::Material *material, GPUMaterial *gpumat);
  void volume_add(Object *ob);
  void render(const DRWView *drw_view,
              RaytraceBuffer &rtbuffer_opaque,
              RaytraceBuffer &rtbuffer_refract,
              GPUTexture *depth_tx,
              GPUTexture *combined_tx);
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Utility texture
 *
 * 64x64 2D array texture containing LUT tables and blue noises.
 * \{ */

class UtilityTexture : public Texture {
  struct Layer {
    float data[UTIL_TEX_SIZE * UTIL_TEX_SIZE][4];
  };

  static constexpr int lut_size = UTIL_TEX_SIZE;
  static constexpr int lut_size_sqr = lut_size * lut_size;
  static constexpr int layer_count = 4 + UTIL_BTDF_LAYER_COUNT;

 public:
  UtilityTexture() : Texture("UtilityTx", GPU_RGBA16F, int2(lut_size), layer_count, nullptr)
  {
#ifdef RUNTIME_LUT_CREATION
    float *bsdf_ggx_lut = EEVEE_lut_update_ggx_brdf(lut_size);
    float(*btdf_ggx_lut)[lut_size_sqr * 2] = (float(*)[lut_size_sqr * 2])
        EEVEE_lut_update_ggx_btdf(lut_size, UTIL_BTDF_LAYER_COUNT);
#else
    const float *bsdf_ggx_lut = bsdf_split_sum_ggx;
    const float(*btdf_ggx_lut)[lut_size_sqr * 2] = btdf_split_sum_ggx;
#endif

    Vector<Layer> data(layer_count);
    {
      Layer &layer = data[UTIL_BLUE_NOISE_LAYER];
      memcpy(layer.data, blue_noise, sizeof(layer));
    }
    {
      Layer &layer = data[UTIL_LTC_MAT_LAYER];
      memcpy(layer.data, ltc_mat_ggx, sizeof(layer));
    }
    {
      Layer &layer = data[UTIL_LTC_MAG_LAYER];
      for (auto i : IndexRange(lut_size_sqr)) {
        layer.data[i][0] = bsdf_ggx_lut[i * 2 + 0];
        layer.data[i][1] = bsdf_ggx_lut[i * 2 + 1];
        layer.data[i][2] = ltc_mag_ggx[i * 2 + 0];
        layer.data[i][3] = ltc_mag_ggx[i * 2 + 1];
      }
      BLI_assert(UTIL_LTC_MAG_LAYER == UTIL_BSDF_LAYER);
    }
    {
      Layer &layer = data[UTIL_DISK_INTEGRAL_LAYER];
      for (auto i : IndexRange(lut_size_sqr)) {
        layer.data[i][UTIL_DISK_INTEGRAL_COMP] = ltc_disk_integral[i];
      }
    }
    {
      for (auto layer_id : IndexRange(16)) {
        Layer &layer = data[3 + layer_id];
        for (auto i : IndexRange(lut_size_sqr)) {
          layer.data[i][0] = btdf_ggx_lut[layer_id][i * 2 + 0];
          layer.data[i][1] = btdf_ggx_lut[layer_id][i * 2 + 1];
        }
      }
    }
    GPU_texture_update_mipmap(*this, 0, GPU_DATA_FLOAT, data.data());
  }

  ~UtilityTexture(){};
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name ShadingPasses
 *
 * \{ */

/**
 * Shading passes. Shared between views. Objects will subscribe to one of them.
 */
class ShadingPasses {
 public:
  BackgroundPass background;
  DeferredPass deferred;
  ForwardPass forward;
  ShadowPass shadow;
  VelocityPass velocity;

  UtilityTexture utility_tx;

 public:
  ShadingPasses(Instance &inst)
      : background(inst), deferred(inst), forward(inst), shadow(inst), velocity(inst){};

  void sync()
  {
    deferred.sync();
    forward.sync();
    shadow.sync();
    velocity.sync();
  }

  DRWShadingGroup *material_add(::Material *blender_mat,
                                GPUMaterial *gpumat,
                                eMaterialPipeline pipeline_type)
  {
    switch (pipeline_type) {
      case MAT_PIPE_DEFERRED_PREPASS:
        return deferred.prepass_add(blender_mat, gpumat);
      case MAT_PIPE_FORWARD_PREPASS:
        return forward.prepass_add(blender_mat, gpumat);
      case MAT_PIPE_DEFERRED:
        return deferred.material_add(blender_mat, gpumat);
      case MAT_PIPE_FORWARD:
        return forward.material_add(blender_mat, gpumat);
      case MAT_PIPE_VOLUME:
        /* TODO(fclem) volume pass. */
        return nullptr;
      case MAT_PIPE_SHADOW:
        return shadow.material_add(blender_mat, gpumat);
    }
    return nullptr;
  }
};

/** \} */

}  // namespace blender::eevee