/**
 * Copy input depth texture to lower left corner of the destination, filling any padding with
 * clamped texture extrapolation.
 */

#pragma BLENDER_REQUIRE(common_view_lib.glsl)

void main()
{
  vec2 uv = gl_FragCoord.xy / vec2(textureSize(depth_tx, 0).xy);

  out_depth = texture(depth_tx, uv).r;
}
