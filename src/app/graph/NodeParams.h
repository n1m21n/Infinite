// Control ranges (owner 2026-11-24, tools/design/param_tables.py made the first cut; edit by hand from here).
// One table per node, in NodeRegistry.cpp's registration order, named for the node class; then one table per
// panel for its own settings. A value here is only the control's range (lo / hi), drag speed, default or step:
// saved patches, param names, order and defaults do not read it, so changing nothing here changes no data.
#pragma once

namespace params
{
   // Slideshow (REGISTER_NODE SlideshowNode)
   namespace Slideshow
   {
      constexpr float hold_duration_lo = 0.05f;
      constexpr float hold_duration_hi = 60.0f;
      constexpr float transition_duration_hi = 15.0f;
      constexpr float transition_duration_lo = 0.0f;
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // Shape (REGISTER_NODE ShapeNode)
   namespace Shape
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
      constexpr float size_x_lo = 0.01f;
      constexpr float size_x_hi = 1.0f;
      constexpr float size_y_lo = 0.01f;
      constexpr float size_y_hi = 1.0f;
      constexpr float corner_radius_hi = 0.3f;
      constexpr float corner_radius_lo = 0.0f;
      constexpr int sides_lo = 3;
      constexpr int sides_hi = 20;
      constexpr float inner_ratio_lo = 0.05f;
      constexpr float inner_ratio_hi = 1.0f;
      constexpr float rotation_lo = -180.0f;
      constexpr float rotation_hi = 180.0f;
      constexpr float stroke_width_hi = 0.1f;
      constexpr float stroke_width_lo = 0.0f;
      constexpr float feather_hi = 0.1f;
      constexpr float feather_lo = 0.0f;
   }

   // Formula (REGISTER_NODE FormulaNode)
   namespace Formula
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // FieldPixel (REGISTER_NODE FieldPixelNode)
   namespace FieldPixel
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // Sketch (REGISTER_NODE SketchNode)
   namespace Sketch
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // Text (REGISTER_NODE TextNode)
   namespace Text
   {
      constexpr float font_size_lo = 8.0f;
      constexpr float font_size_hi = 300.0f;
      constexpr float tracking_lo = -10.0f;
      constexpr float tracking_hi = 40.0f;
      constexpr float scale_x_lo = 0.1f;
      constexpr float scale_x_hi = 4.0f;
      constexpr float scale_y_lo = 0.1f;
      constexpr float scale_y_hi = 4.0f;
      constexpr float wrap_width_lo = 0.1f;
      constexpr float wrap_width_hi = 1.0f;
      constexpr float wrap_height_lo = 0.1f;
      constexpr float wrap_height_hi = 1.0f;
      constexpr float line_spacing_lo = 0.5f;
      constexpr float line_spacing_hi = 2.5f;
      constexpr float outline_width_hi = 20.0f;
      constexpr float outline_width_lo = 0.0f;
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // Video (REGISTER_NODE VideoSourceNode)
   namespace VideoSource
   {
      constexpr float speed_lo = -2.0f;
      constexpr float speed_hi = 4.0f;
   }

   // VMPC (REGISTER_NODE VmpcNode)
   namespace Vmpc
   {
      constexpr float transition_ms_hi = 2000.0f;
      constexpr float transition_ms_lo = 0.0f;
   }

   // Noise (REGISTER_NODE NoiseNode)
   namespace Noise
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
      constexpr float scale_lo = 0.5f;
      constexpr float scale_hi = 60.0f;
      constexpr float octaves_hi = 8.0f;
      constexpr float octaves_lo = 1.0f;
      constexpr float lacunarity_hi = 4.0f;
      constexpr float lacunarity_lo = 1.0f;
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 0.9f;
      constexpr float warp_hi = 2.0f;
      constexpr float warp_lo = 0.0f;
      constexpr float speed_lo = -2.0f;
      constexpr float speed_hi = 2.0f;
      constexpr float contrast_lo = 0.1f;
      constexpr float contrast_hi = 4.0f;
      constexpr float brightness_lo = -0.5f;
      constexpr float brightness_hi = 0.5f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Texture (REGISTER_NODE TextureNode)
   namespace Texture
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
      constexpr float scale_lo = 0.5f;
      constexpr float scale_hi = 60.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr float voronoi_minkowski_exponent_lo = 0.1f;
      constexpr float voronoi_minkowski_exponent_hi = 8.0f;
      constexpr float voronoi_smoothness_lo = 0.001f;
      constexpr float voronoi_smoothness_hi = 1.0f;
      constexpr float brick_width_lo = 0.05f;
      constexpr float brick_width_hi = 2.0f;
      constexpr float brick_height_lo = 0.05f;
      constexpr float brick_height_hi = 2.0f;
      constexpr float brick_mortar_size_hi = 0.2f;
      constexpr float brick_mortar_size_lo = 0.0f;
      constexpr float brick_bias_lo = -0.5f;
      constexpr float brick_bias_hi = 0.5f;
      constexpr float magic_depth_hi = 10.0f;
      constexpr float magic_depth_lo = 1.0f;
      constexpr float magic_distortion_hi = 4.0f;
      constexpr float magic_distortion_lo = 0.0f;
      constexpr float wave_distortion_hi = 4.0f;
      constexpr float wave_distortion_lo = 0.0f;
      constexpr float wave_detail_hi = 8.0f;
      constexpr float wave_detail_lo = 1.0f;
      constexpr float wave_detail_scale_lo = 0.1f;
      constexpr float wave_detail_scale_hi = 8.0f;
      constexpr float musgrave_dimension_hi = 4.0f;
      constexpr float musgrave_dimension_lo = 0.0f;
      constexpr float musgrave_lacunarity_lo = 1.001f;
      constexpr float musgrave_lacunarity_hi = 6.0f;
      constexpr float musgrave_octaves_hi = 8.0f;
      constexpr float musgrave_octaves_lo = 1.0f;
      constexpr float musgrave_gain_hi = 4.0f;
      constexpr float musgrave_gain_lo = 0.0f;
      constexpr float musgrave_offset_hi = 4.0f;
      constexpr float musgrave_offset_lo = 0.0f;
      constexpr float clouds_depth_hi = 8.0f;
      constexpr float clouds_depth_lo = 1.0f;
      constexpr float marble_turbulence_hi = 20.0f;
      constexpr float marble_turbulence_lo = 0.0f;
      constexpr float marble_noise_scale_lo = 0.1f;
      constexpr float marble_noise_scale_hi = 8.0f;
      constexpr float marble_noise_depth_hi = 8.0f;
      constexpr float marble_noise_depth_lo = 1.0f;
      constexpr float wood_turbulence_hi = 20.0f;
      constexpr float wood_turbulence_lo = 0.0f;
      constexpr float wood_noise_scale_lo = 0.1f;
      constexpr float wood_noise_scale_hi = 8.0f;
      constexpr float contrast_lo = 0.1f;
      constexpr float contrast_hi = 4.0f;
      constexpr float brightness_lo = -0.5f;
      constexpr float brightness_hi = 0.5f;
   }

   // Ramp (REGISTER_NODE RampNode)
   namespace Ramp
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
      constexpr float angle_lo = -180.0f;
      constexpr float angle_hi = 180.0f;
      constexpr float center_x_lo = -0.5f;
      constexpr float center_x_hi = 1.5f;
      constexpr float center_y_lo = -0.5f;
      constexpr float center_y_hi = 1.5f;
      constexpr float scale_lo = 0.05f;
      constexpr float scale_hi = 8.0f;
      constexpr float gamma_lo = 0.1f;
      constexpr float gamma_hi = 4.0f;
      constexpr int stop_count_lo = 2;
   }

   // Geometry (REGISTER_NODE GeometryNode)
   namespace Geometry
   {
      constexpr int detail_lo = 2;
      constexpr int detail_hi = 96;
      constexpr float super_n2_lo = 0.1f;
      constexpr float super_n2_hi = 8.0f;
      constexpr float super_n3_lo = 0.1f;
      constexpr float super_n3_hi = 8.0f;
      constexpr float super_p2_lo = 0.1f;
      constexpr float super_p2_hi = 8.0f;
      constexpr float super_p3_lo = 0.1f;
      constexpr float super_p3_hi = 8.0f;
      constexpr int bevel_segments_hi = 3;
      constexpr int bevel_segments_lo = 1;
      constexpr int sides_lo = 3;
      constexpr int sides_hi = 64;
      constexpr float tube_radius_lo = 0.02f;
      constexpr float tube_radius_hi = 0.95f;
      constexpr float tube_radius_2_hi = 0.99f;
      constexpr float tube_radius_2_lo = 0.0f;
      constexpr float super_n2_2_hi = 2.0f;
      constexpr float super_n3_2_hi = 2.0f;
      constexpr float super_n3_2_lo = 0.0f;
      constexpr float super_n2_3_hi = 1.9f;
      constexpr float disc_inner_hi = 0.98f;
      constexpr float disc_inner_lo = 0.0f;
      constexpr float tube_radius_3_hi = 0.8f;
      constexpr float super_n2_2_lo = 0.15f;
      constexpr float super_n2_4_hi = 2.5f;
      constexpr int knot_p_hi = 8;
      constexpr int knot_p_lo = 1;
      constexpr int knot_q_hi = 8;
      constexpr int knot_q_lo = 1;
   }

   // Model 3D (REGISTER_NODE ModelSourceNode)
   namespace ModelSource
   {
      constexpr float clip_speed_lo = -2.0f;
      constexpr float clip_speed_hi = 4.0f;
      constexpr float clip_beats_lo = 0.25f;
      constexpr float clip_beats_hi = 64.0f;
      constexpr float point_size_lo = 0.0005f;
      constexpr float point_size_hi = 0.05f;
   }

   // Text 3D (REGISTER_NODE Text3DNode)
   namespace Text3D
   {
      constexpr float depth_hi = 1.5f;
      constexpr float depth_lo = 0.0f;
      constexpr float bevel_hi = 0.45f;
      constexpr float bevel_lo = 0.0f;
      constexpr float letter_spacing_lo = -0.1f;
      constexpr float letter_spacing_hi = 0.5f;
      constexpr float pos_x_lo = -10.0f;
      constexpr float pos_x_hi = 10.0f;
      constexpr float pos_y_lo = -10.0f;
      constexpr float pos_y_hi = 10.0f;
      constexpr float pos_z_lo = -10.0f;
      constexpr float pos_z_hi = 10.0f;
      constexpr float rot_x_lo = -3.14159f;
      constexpr float rot_x_hi = 3.14159f;
      constexpr float rot_y_lo = -3.14159f;
      constexpr float rot_y_hi = 3.14159f;
      constexpr float rot_z_lo = -3.14159f;
      constexpr float rot_z_hi = 3.14159f;
      constexpr float uniform_scale_lo = 0.05f;
      constexpr float uniform_scale_hi = 10.0f;
      constexpr float spin_y_lo = -2.0f;
      constexpr float spin_y_hi = 2.0f;
   }

   // Ocean (REGISTER_NODE OceanNode)
   namespace Ocean
   {
      constexpr int resolution_lo = 8;
      constexpr int resolution_hi = 300;
      constexpr float size_lo = 0.5f;
      constexpr float size_hi = 20.0f;
      constexpr float horizon_hi = 100.0f;
      constexpr float horizon_lo = 1.0f;
      constexpr float wavelength_lo = 0.1f;
      constexpr float wavelength_hi = 8.0f;
      constexpr float steepness_hi = 2.0f;
      constexpr float steepness_lo = 0.0f;
      constexpr float choppiness_hi = 2.0f;
      constexpr float choppiness_lo = 0.0f;
      constexpr float direction_lo = -180.0f;
      constexpr float direction_hi = 180.0f;
      constexpr int octaves_hi = 8;
      constexpr int octaves_lo = 1;
      constexpr float speed_lo = -3.0f;
      constexpr float speed_hi = 3.0f;
   }

   // Material (REGISTER_NODE MaterialNode)
   namespace Material
   {
      constexpr float normal_strength_hi = 4.0f;
      constexpr float normal_strength_lo = 0.0f;
      constexpr float detail_scale_hi = 32.0f;
      constexpr float detail_scale_lo = 0.0f;
      constexpr float detail_strength_hi = 4.0f;
      constexpr float detail_strength_lo = 0.0f;
      constexpr float roughness_lo = 0.02f;
      constexpr float roughness_hi = 1.0f;
      constexpr float ior_hi = 3.0f;
      constexpr float ior_lo = 1.0f;
      constexpr float emission_hi = 8.0f;
      constexpr float emission_lo = 0.0f;
      constexpr float clearcoat_roughness_lo = 0.02f;
      constexpr float clearcoat_roughness_hi = 1.0f;
      constexpr float sheen_roughness_lo = 0.01f;
      constexpr float sheen_roughness_hi = 1.0f;
      constexpr float iridescence_ior_hi = 3.0f;
      constexpr float iridescence_ior_lo = 1.0f;
      constexpr float iridescence_thickness_lo = 100.0f;
      constexpr float iridescence_thickness_hi = 1000.0f;
      constexpr float radial_roughness_lo = 0.02f;
      constexpr float radial_roughness_hi = 1.0f;
      constexpr float hair_shift_lo = -10.0f;
      constexpr float hair_shift_hi = 10.0f;
   }

   // Displacement (REGISTER_NODE DisplacementNode)
   namespace Displacement
   {
      constexpr float strength_lo = -2.0f;
      constexpr float strength_hi = 2.0f;
      constexpr float frequency_lo = 0.01f;
      constexpr float frequency_hi = 20.0f;
      constexpr float speed_hi = 10.0f;
      constexpr float speed_lo = 0.0f;
      constexpr int octaves_hi = 8;
      constexpr int octaves_lo = 1;
      constexpr int seed_hi = 999;
      constexpr int seed_lo = 0;
   }

   // Audio Displacement (REGISTER_NODE AudioDisplacementNode)
   namespace AudioDisplacement
   {
      constexpr float strength_lo = -4.0f;
      constexpr float strength_hi = 4.0f;
      constexpr float frequency_lo = 0.1f;
      constexpr float frequency_hi = 12.0f;
      constexpr float speed_lo = -5.0f;
      constexpr float speed_hi = 5.0f;
      constexpr float damping_hi = 4.0f;
      constexpr float damping_lo = 0.0f;
      constexpr float attack_hi = 300.0f;
      constexpr float attack_lo = 1.0f;
      constexpr float decay_lo = 10.0f;
      constexpr float decay_hi = 1500.0f;
      constexpr int subdivide_hi = 4;
      constexpr int subdivide_lo = 0;
      constexpr int chladni_m_hi = 12;
      constexpr int chladni_m_lo = 1;
      constexpr int chladni_n_hi = 12;
      constexpr int chladni_n_lo = 1;
      constexpr float roughness_lo = 0.02f;
      constexpr float roughness_hi = 1.0f;
      constexpr float emission_hi = 8.0f;
      constexpr float emission_lo = 0.0f;
   }

   // Audio Ribbon (REGISTER_NODE AudioRibbonNode)
   namespace AudioRibbon
   {
      constexpr float width_lo = 0.05f;
      constexpr float width_hi = 5.0f;
      constexpr float height_scale_hi = 5.0f;
      constexpr float height_scale_lo = 0.0f;
      constexpr int segments_lo = 16;
      constexpr int segments_hi = 1024;
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 10.0f;
      constexpr float smoothing_hi = 0.99f;
      constexpr float smoothing_lo = 0.0f;
      constexpr float pos_x_lo = -10.0f;
      constexpr float pos_x_hi = 10.0f;
      constexpr float pos_y_lo = -10.0f;
      constexpr float pos_y_hi = 10.0f;
      constexpr float pos_z_lo = -10.0f;
      constexpr float pos_z_hi = 10.0f;
      constexpr float rot_x_lo = -3.14159f;
      constexpr float rot_x_hi = 3.14159f;
      constexpr float rot_y_lo = -3.14159f;
      constexpr float rot_y_hi = 3.14159f;
      constexpr float rot_z_lo = -3.14159f;
      constexpr float rot_z_hi = 3.14159f;
      constexpr float scale_x_lo = 0.01f;
      constexpr float scale_x_hi = 10.0f;
      constexpr float scale_y_lo = 0.01f;
      constexpr float scale_y_hi = 10.0f;
      constexpr float scale_z_lo = 0.01f;
      constexpr float scale_z_hi = 10.0f;
      constexpr float uniform_scale_lo = 0.01f;
      constexpr float uniform_scale_hi = 10.0f;
      constexpr float roughness_lo = 0.02f;
      constexpr float roughness_hi = 1.0f;
      constexpr float emission_hi = 8.0f;
      constexpr float emission_lo = 0.0f;
      constexpr float ior_hi = 3.0f;
      constexpr float ior_lo = 1.0f;
   }

   // Mapping (REGISTER_NODE MappingNode)
   namespace Mapping
   {
      constexpr float translate_x_lo = -4.0f;
      constexpr float translate_x_hi = 4.0f;
      constexpr float translate_y_lo = -4.0f;
      constexpr float translate_y_hi = 4.0f;
      constexpr float translate_z_lo = -4.0f;
      constexpr float translate_z_hi = 4.0f;
      constexpr float rotate_z_lo = -180.0f;
      constexpr float rotate_z_hi = 180.0f;
      constexpr float rotate_x_lo = -180.0f;
      constexpr float rotate_x_hi = 180.0f;
      constexpr float rotate_y_lo = -180.0f;
      constexpr float rotate_y_hi = 180.0f;
      constexpr float scale_x_lo = 0.05f;
      constexpr float scale_x_hi = 8.0f;
      constexpr float scale_y_lo = 0.05f;
      constexpr float scale_y_hi = 8.0f;
      constexpr float scale_z_lo = 0.05f;
      constexpr float scale_z_hi = 8.0f;
   }

   // Shade Mapping (REGISTER_NODE ShadeMappingNode)
   namespace ShadeMapping
   {
      constexpr float translate_0_lo = -4.0f;
      constexpr float translate_0_hi = 4.0f;
      constexpr float translate_1_lo = -4.0f;
      constexpr float translate_1_hi = 4.0f;
      constexpr float translate_2_lo = -4.0f;
      constexpr float translate_2_hi = 4.0f;
      constexpr float rotate_0_lo = -180.0f;
      constexpr float rotate_0_hi = 180.0f;
      constexpr float rotate_1_lo = -180.0f;
      constexpr float rotate_1_hi = 180.0f;
      constexpr float rotate_2_lo = -180.0f;
      constexpr float rotate_2_hi = 180.0f;
      constexpr float scale_0_lo = 0.05f;
      constexpr float scale_0_hi = 8.0f;
      constexpr float scale_1_lo = 0.05f;
      constexpr float scale_1_hi = 8.0f;
      constexpr float scale_2_lo = 0.05f;
      constexpr float scale_2_hi = 8.0f;
   }

   // Shade Checker (REGISTER_NODE ShadeCheckerNode)
   namespace ShadeChecker
   {
      constexpr float scale_lo = 0.1f;
      constexpr float scale_hi = 32.0f;
   }

   // Shade Noise (REGISTER_NODE ShadeNoiseNode)
   namespace ShadeNoise
   {
      constexpr float scale_lo = 0.1f;
      constexpr float scale_hi = 32.0f;
      constexpr float detail_hi = 8.0f;
      constexpr float detail_lo = 0.0f;
      constexpr float distortion_hi = 4.0f;
      constexpr float distortion_lo = 0.0f;
   }

   // Shade Voronoi (REGISTER_NODE ShadeVoronoiNode)
   namespace ShadeVoronoi
   {
      constexpr float scale_lo = 0.1f;
      constexpr float scale_hi = 32.0f;
   }

   // Shade Gradient (REGISTER_NODE ShadeGradientNode)
   namespace ShadeGradient
   {
      constexpr float scale_lo = 0.1f;
      constexpr float scale_hi = 32.0f;
   }

   // Shade Brick (REGISTER_NODE ShadeBrickNode)
   namespace ShadeBrick
   {
      constexpr float scale_lo = 0.1f;
      constexpr float scale_hi = 32.0f;
      constexpr float width_lo = 0.05f;
      constexpr float width_hi = 2.0f;
      constexpr float height_lo = 0.05f;
      constexpr float height_hi = 2.0f;
      constexpr float mortar_hi = 0.125f;
      constexpr float mortar_lo = 0.0f;
   }

   // Shade Math (REGISTER_NODE ShadeMathNode)
   namespace ShadeMath
   {
      constexpr float value_lo = -10.0f;
      constexpr float value_hi = 10.0f;
   }

   // Shade Vector Math (REGISTER_NODE ShadeVectorMathNode)
   namespace ShadeVectorMath
   {
      constexpr float scale_lo = -10.0f;
      constexpr float scale_hi = 10.0f;
   }

   // Shade Mix (REGISTER_NODE ShadeMixNode)
   namespace ShadeMix
   {
      constexpr float a_0_lo = -10.0f;
      constexpr float a_0_hi = 10.0f;
      constexpr float b_0_lo = -10.0f;
      constexpr float b_0_hi = 10.0f;
      constexpr float a_lo = -10.0f;
      constexpr float a_hi = 10.0f;
      constexpr float b_lo = -10.0f;
      constexpr float b_hi = 10.0f;
   }

   // Shade Ramp (REGISTER_NODE ShadeRampNode)
   namespace ShadeRamp
   {
      constexpr int count_lo = 2;
   }

   // Shade Map Range (REGISTER_NODE ShadeMapRangeNode)
   namespace ShadeMapRange
   {
      constexpr float value_lo = -10.0f;
      constexpr float value_hi = 10.0f;
      constexpr float from_min_lo = -10.0f;
      constexpr float from_min_hi = 10.0f;
      constexpr float from_max_lo = -10.0f;
      constexpr float from_max_hi = 10.0f;
      constexpr float to_min_lo = -10.0f;
      constexpr float to_min_hi = 10.0f;
      constexpr float to_max_lo = -10.0f;
      constexpr float to_max_hi = 10.0f;
      constexpr float steps_hi = 32.0f;
      constexpr float steps_lo = 1.0f;
   }

   // Shade Curves (REGISTER_NODE ShadeCurvesNode)
   namespace ShadeCurves
   {
      constexpr int count_lo = 2;
   }

   // Shade Normal Map (REGISTER_NODE ShadeNormalMapNode)
   namespace ShadeNormalMap
   {
      constexpr float strength_hi = 2.0f;
      constexpr float strength_lo = 0.0f;
   }

   // Shade Fresnel (REGISTER_NODE ShadeFresnelNode)
   namespace ShadeFresnel
   {
      constexpr float ior_hi = 3.0f;
      constexpr float ior_lo = 1.0f;
   }

   // Particle System (REGISTER_NODE ParticleSystemNode)
   namespace ParticleSystem
   {
      constexpr int max_particles_lo = 16;
      constexpr float emit_rate_hi = 3000.0f;
      constexpr float emit_rate_lo = 0.0f;
      constexpr float emit_radius_hi = 3.0f;
      constexpr float emit_radius_lo = 0.0f;
      constexpr float lifetime_lo = 0.1f;
      constexpr float lifetime_hi = 20.0f;
      constexpr float initial_speed_hi = 10.0f;
      constexpr float initial_speed_lo = 0.0f;
      constexpr float gravity_x_lo = -10.0f;
      constexpr float gravity_x_hi = 10.0f;
      constexpr float gravity_y_lo = -10.0f;
      constexpr float gravity_y_hi = 10.0f;
      constexpr float gravity_z_lo = -10.0f;
      constexpr float gravity_z_hi = 10.0f;
      constexpr float drag_hi = 5.0f;
      constexpr float drag_lo = 0.0f;
      constexpr float turbulence_hi = 10.0f;
      constexpr float turbulence_lo = 0.0f;
      constexpr float turbulence_scale_lo = 0.1f;
      constexpr float turbulence_scale_hi = 8.0f;
      constexpr float start_size_hi = 0.5f;
      constexpr float start_size_lo = 0.0f;
      constexpr float end_size_hi = 0.5f;
      constexpr float end_size_lo = 0.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr float speed_along_normal_lo = -10.0f;
      constexpr float speed_along_normal_hi = 10.0f;
      constexpr float mass_lo = 0.1f;
      constexpr float mass_hi = 10.0f;
      constexpr float time_scale_hi = 4.0f;
      constexpr float time_scale_lo = 0.0f;
      constexpr float size_curve_lo = 0.1f;
      constexpr float size_curve_hi = 10.0f;
      constexpr float color_curve_lo = 0.1f;
      constexpr float color_curve_hi = 10.0f;
   }

   // Cloth (REGISTER_NODE ClothNode)
   namespace Cloth
   {
      constexpr int iterations_hi = 40;
      constexpr int iterations_lo = 1;
      constexpr float damping_hi = 0.5f;
      constexpr float damping_lo = 0.0f;
      constexpr float mass_lo = 0.05f;
      constexpr float mass_hi = 10.0f;
      constexpr float gravity_x_lo = -20.0f;
      constexpr float gravity_x_hi = 20.0f;
      constexpr float gravity_y_lo = -20.0f;
      constexpr float gravity_y_hi = 20.0f;
      constexpr float gravity_z_lo = -20.0f;
      constexpr float gravity_z_hi = 20.0f;
      constexpr float wind_x_lo = -20.0f;
      constexpr float wind_x_hi = 20.0f;
      constexpr float wind_y_lo = -20.0f;
      constexpr float wind_y_hi = 20.0f;
      constexpr float wind_z_lo = -20.0f;
      constexpr float wind_z_hi = 20.0f;
      constexpr float wind_turbulence_hi = 20.0f;
      constexpr float wind_turbulence_lo = 0.0f;
      constexpr float ground_height_lo = -5.0f;
      constexpr float ground_height_hi = 5.0f;
      constexpr float collider_distance_hi = 0.2f;
      constexpr float collider_distance_lo = 0.0f;
      constexpr float collider_friction_hi = 10.0f;
      constexpr float collider_friction_lo = 0.0f;
   }

   // Rigid Bodies (REGISTER_NODE RigidBodyNode)
   namespace RigidBody
   {
      constexpr float mass_lo = 0.05f;
      constexpr float mass_hi = 20.0f;
      constexpr float linear_damping_hi = 2.0f;
      constexpr float linear_damping_lo = 0.0f;
      constexpr float angular_damping_hi = 2.0f;
      constexpr float angular_damping_lo = 0.0f;
      constexpr int iterations_hi = 50;
      constexpr int iterations_lo = 1;
      constexpr float gravity_x_lo = -30.0f;
      constexpr float gravity_x_hi = 30.0f;
      constexpr float gravity_y_lo = -30.0f;
      constexpr float gravity_y_hi = 30.0f;
      constexpr float gravity_z_lo = -30.0f;
      constexpr float gravity_z_hi = 30.0f;
      constexpr float vel_x_lo = -20.0f;
      constexpr float vel_x_hi = 20.0f;
      constexpr float vel_y_lo = -20.0f;
      constexpr float vel_y_hi = 20.0f;
      constexpr float vel_z_lo = -20.0f;
      constexpr float vel_z_hi = 20.0f;
      constexpr float spin_x_lo = -20.0f;
      constexpr float spin_x_hi = 20.0f;
      constexpr float spin_y_lo = -20.0f;
      constexpr float spin_y_hi = 20.0f;
      constexpr float spin_z_lo = -20.0f;
      constexpr float spin_z_hi = 20.0f;
      constexpr float kick_time_hi = 20.0f;
      constexpr float kick_time_lo = 0.0f;
      constexpr float kick_x_lo = -20.0f;
      constexpr float kick_x_hi = 20.0f;
      constexpr float kick_y_lo = -20.0f;
      constexpr float kick_y_hi = 20.0f;
      constexpr float kick_z_lo = -20.0f;
      constexpr float kick_z_hi = 20.0f;
      constexpr float ground_height_lo = -10.0f;
      constexpr float ground_height_hi = 10.0f;
      constexpr float wall_size_lo = 0.5f;
      constexpr float wall_size_hi = 20.0f;
   }

   // L-System (REGISTER_NODE LSystemNode)
   namespace LSystem
   {
      constexpr float angle_hi = 180.0f;
      constexpr float angle_lo = 0.0f;
      constexpr float length_lo = 0.005f;
      constexpr float length_hi = 1.0f;
      constexpr float length_scale_lo = 0.1f;
      constexpr float length_scale_hi = 1.5f;
      constexpr float radius_lo = 0.001f;
      constexpr float radius_hi = 0.2f;
      constexpr float radius_scale_lo = 0.1f;
      constexpr float radius_scale_hi = 1.5f;
      constexpr int sides_lo = 3;
      constexpr int sides_hi = 24;
   }

   // Metaballs (REGISTER_NODE MetaBallNode)
   namespace MetaBall
   {
      constexpr float spread_hi = 2.0f;
      constexpr float spread_lo = 0.0f;
      constexpr int max_from_cloud_hi = 64;
      constexpr int max_from_cloud_lo = 1;
      constexpr float radius_lo = 0.05f;
      constexpr float radius_hi = 1.5f;
      constexpr float threshold_lo = 0.5f;
      constexpr float threshold_hi = 40.0f;
      constexpr float bounds_lo = 0.5f;
      constexpr float bounds_hi = 6.0f;
      constexpr int resolution_lo = 8;
      constexpr int resolution_hi = 96;
   }

   // Resynthesize 3D (REGISTER_NODE MeshResynthNode)
   namespace MeshResynth
   {
      constexpr float chaos_hi = 1.5f;
      constexpr float chaos_lo = 0.0f;
      constexpr float steps_per_beat_lo = 0.05f;
      constexpr float steps_per_beat_hi = 8.0f;
      constexpr float seed_hi = 1000.0f;
      constexpr float seed_lo = 0.0f;
      constexpr int triangle_budget_lo = 2000;
      constexpr int triangle_budget_hi = 500000;
   }

   // Image to Points (REGISTER_NODE ImageToPointsNode)
   namespace ImageToPoints
   {
      constexpr int density_lo = 4;
      constexpr int density_hi = 400;
      constexpr float width_lo = 0.1f;
      constexpr float width_hi = 8.0f;
      constexpr float height_lo = 0.1f;
      constexpr float height_hi = 8.0f;
      constexpr float depth_scale_lo = -4.0f;
      constexpr float depth_scale_hi = 4.0f;
      constexpr float point_size_lo = 0.01f;
      constexpr float point_size_hi = 4.0f;
   }

   // Depth Projection (REGISTER_NODE DepthProjectionNode)
   namespace DepthProjection
   {
      constexpr int density_lo = 8;
      constexpr int density_hi = 400;
      constexpr float near_depth_lo = 0.01f;
      constexpr float near_depth_hi = 20.0f;
      constexpr float far_depth_lo = 0.1f;
      constexpr float far_depth_hi = 50.0f;
      constexpr float depth_scale_lo = -5.0f;
      constexpr float depth_scale_hi = 5.0f;
      constexpr float depth_curve_lo = 0.1f;
      constexpr float depth_curve_hi = 4.0f;
      constexpr float fov_lo = 10.0f;
      constexpr float fov_hi = 130.0f;
      constexpr float custom_aspect_lo = 0.2f;
      constexpr float custom_aspect_hi = 4.0f;
      constexpr float focal_scale_lo = 0.1f;
      constexpr float focal_scale_hi = 3.0f;
      constexpr float planar_width_lo = 0.1f;
      constexpr float planar_width_hi = 20.0f;
      constexpr float planar_height_lo = 0.1f;
      constexpr float planar_height_hi = 20.0f;
      constexpr float point_size_lo = 0.01f;
      constexpr float point_size_hi = 4.0f;
      constexpr float edge_tear_threshold_lo = 0.01f;
      constexpr float edge_tear_threshold_hi = 2.0f;
   }

   // Curve (REGISTER_NODE CurveNode)
   namespace Curve
   {
      constexpr int point_count_lo = 2;
      constexpr int segments_hi = 64;
      constexpr int segments_lo = 1;
      constexpr float spread_hi = 4.0f;
      constexpr float spread_lo = 0.0f;
      constexpr float height_lo = -3.0f;
      constexpr float height_hi = 3.0f;
      constexpr float twist_lo = -180.0f;
      constexpr float twist_hi = 180.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr float radius_hi = 0.5f;
      constexpr float radius_lo = 0.0f;
      constexpr int sides_lo = 3;
      constexpr int sides_hi = 32;
   }

   // Instance on Points (REGISTER_NODE InstanceOnPointsNode)
   namespace InstanceOnPoints
   {
      constexpr int max_points_hi = 20000;
      constexpr int max_points_lo = 1;
      constexpr float instance_scale_lo = 0.0005f;
      constexpr float instance_scale_hi = 1.0f;
      constexpr float normal_offset_lo = -0.5f;
      constexpr float normal_offset_hi = 0.5f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Set Vertex Color (REGISTER_NODE SetColorNode)
   namespace SetColor
   {
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr int palette_offset_hi = 32;
      constexpr int palette_offset_lo = 0;
   }

   // Wrap (REGISTER_NODE WrapNode)
   namespace Wrap
   {
      constexpr float radius_scale_lo = 0.05f;
      constexpr float radius_scale_hi = 3.0f;
      constexpr float radius_override_lo = 0.05f;
      constexpr float radius_override_hi = 5.0f;
      constexpr float offset_lo = -0.5f;
      constexpr float offset_hi = 0.5f;
   }

   // Field Modifier (REGISTER_NODE FieldElementNode)
   namespace FieldElement
   {
      constexpr int max_elements_lo = 100;
      constexpr int max_elements_hi = 200000;
      constexpr int generate_count_hi = 100000;
      constexpr int generate_count_lo = 1;
   }

   // Field Primitive (REGISTER_NODE FieldPrimitiveNode)
   namespace FieldPrimitive
   {
      constexpr int count_hi = 100000;
      constexpr int count_lo = 1;
      constexpr int max_elements_lo = 100;
      constexpr int max_elements_hi = 200000;
   }

   // Distribute Points on Faces (REGISTER_NODE DistributePointsOnFacesNode)
   namespace DistributePointsOnFaces
   {
      constexpr float density_hi = 20000.0f;
      constexpr float density_lo = 1.0f;
      constexpr float min_distance_lo = 0.005f;
      constexpr float min_distance_hi = 1.0f;
      constexpr float point_size_lo = 0.002f;
      constexpr float point_size_hi = 0.3f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Proximity Lines (REGISTER_NODE ProximityLinesNode)
   namespace ProximityLines
   {
      constexpr float radius_hi = 5.0f;
      constexpr float radius_lo = 0.0f;
      constexpr int max_per_point_hi = 16;
      constexpr int max_per_point_lo = 1;
      constexpr int max_links_hi = 200000;
      constexpr int max_links_lo = 1;
      constexpr float thickness_lo = 0.0005f;
      constexpr float thickness_hi = 0.2f;
   }

   // Point Trails (REGISTER_NODE PointTrailsNode)
   namespace PointTrails
   {
      constexpr int length_lo = 2;
      constexpr int length_hi = 240;
      constexpr int sample_every_hi = 10;
      constexpr int sample_every_lo = 1;
      constexpr float width_lo = 0.0005f;
      constexpr float width_hi = 0.2f;
      constexpr int point_limit_hi = 100000;
      constexpr int point_limit_lo = 1;
   }

   // Curve Ops (REGISTER_NODE CurveOpsNode)
   namespace CurveOps
   {
      constexpr float tolerance_hi = 0.5f;
      constexpr float tolerance_lo = 0.0f;
      constexpr int iterations_hi = 50;
      constexpr int iterations_lo = 0;
      constexpr float distance_lo = -2.0f;
      constexpr float distance_hi = 2.0f;
      constexpr int arc_steps_hi = 16;
      constexpr int arc_steps_lo = 1;
      constexpr int count_lo = 2;
      constexpr int count_hi = 512;
      constexpr float radius_lo = 0.002f;
      constexpr float radius_hi = 0.2f;
   }

   // Sweep (REGISTER_NODE SweepNode)
   namespace Sweep
   {
      constexpr float profile_radius_lo = 0.002f;
      constexpr float profile_radius_hi = 2.0f;
      constexpr int profile_sides_lo = 3;
      constexpr int profile_sides_hi = 64;
      constexpr float start_scale_hi = 4.0f;
      constexpr float start_scale_lo = 0.0f;
      constexpr float end_scale_hi = 4.0f;
      constexpr float end_scale_lo = 0.0f;
      constexpr float twist_lo = -1080.0f;
      constexpr float twist_hi = 1080.0f;
   }

   // Armature (REGISTER_NODE ArmatureNode)
   namespace Armature
   {
      constexpr int bones_lo = 2;
      constexpr int bones_hi = 32;
      constexpr float envelope_lo = 0.25f;
      constexpr float envelope_hi = 3.0f;
      constexpr float curl_lo = -360.0f;
      constexpr float curl_hi = 360.0f;
      constexpr float curl_direction_hi = 360.0f;
      constexpr float curl_direction_lo = 0.0f;
      constexpr float twist_lo = -360.0f;
      constexpr float twist_hi = 360.0f;
      constexpr float wave_amount_hi = 90.0f;
      constexpr float wave_amount_lo = 0.0f;
      constexpr float wave_length_lo = 0.1f;
      constexpr float wave_length_hi = 4.0f;
      constexpr float wave_speed_lo = -4.0f;
      constexpr float wave_speed_hi = 4.0f;
   }

   // Revolve (REGISTER_NODE RevolveNode)
   namespace Revolve
   {
      constexpr float angle_lo = -360.0f;
      constexpr float angle_hi = 360.0f;
      constexpr int segments_lo = 3;
      constexpr int segments_hi = 256;
   }

   // Delaunay Mesh (REGISTER_NODE DelaunayMeshNode)
   namespace DelaunayMesh
   {
      constexpr int max_points_lo = 3;
      constexpr int max_points_hi = 5000;
      constexpr float inset_hi = 0.95f;
      constexpr float inset_lo = 0.0f;
   }

   // Distribute Points in Grid (REGISTER_NODE DistributePointsInGridNode)
   namespace DistributePointsInGrid
   {
      constexpr int count_x_hi = 200;
      constexpr int count_x_lo = 1;
      constexpr int count_y_hi = 200;
      constexpr int count_y_lo = 1;
      constexpr float spacing_x_lo = 0.01f;
      constexpr float spacing_x_hi = 2.0f;
      constexpr float spacing_y_lo = 0.01f;
      constexpr float spacing_y_hi = 2.0f;
      constexpr float point_size_lo = 0.002f;
      constexpr float point_size_hi = 0.3f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Merge by Distance (REGISTER_NODE MergeByDistanceNode)
   namespace MergeByDistance
   {
      constexpr float threshold_hi = 0.5f;
      constexpr float threshold_lo = 0.0f;
   }

   // Bake (REGISTER_NODE BakeNode)
   namespace Bake
   {
      constexpr int samples_hi = 128;
      constexpr int samples_lo = 1;
      constexpr float distance_lo = 0.01f;
      constexpr float distance_hi = 1.0f;
   }

   // Sculpt (REGISTER_NODE SculptNode)
   namespace Sculpt
   {
      constexpr float amount_hi = 2.0f;
      constexpr float amount_lo = 0.0f;
      constexpr int level_hi = 3;
      constexpr int level_lo = 0;
   }

   // Switcher 3D (REGISTER_NODE Switcher3DNode)
   namespace Switcher3D
   {
      constexpr float interval_lo = 0.05f;
      constexpr float interval_hi = 32.0f;
   }

   // Group 3D (REGISTER_NODE Group3DNode)
   namespace Group3D
   {
      constexpr float offset_x_lo = -3.0f;
      constexpr float offset_x_hi = 3.0f;
      constexpr float offset_y_lo = -3.0f;
      constexpr float offset_y_hi = 3.0f;
      constexpr float offset_z_lo = -3.0f;
      constexpr float offset_z_hi = 3.0f;
      constexpr float rot_x_lo = -180.0f;
      constexpr float rot_x_hi = 180.0f;
      constexpr float rot_y_lo = -180.0f;
      constexpr float rot_y_hi = 180.0f;
      constexpr float rot_z_lo = -180.0f;
      constexpr float rot_z_hi = 180.0f;
      constexpr float scale_lo = 0.05f;
      constexpr float scale_hi = 4.0f;
      constexpr float pivot_x_lo = -3.0f;
      constexpr float pivot_x_hi = 3.0f;
      constexpr float pivot_y_lo = -3.0f;
      constexpr float pivot_y_hi = 3.0f;
      constexpr float pivot_z_lo = -3.0f;
      constexpr float pivot_z_hi = 3.0f;
   }

   // 3D View (REGISTER_NODE Scene3DNode)
   namespace Scene3D
   {
      constexpr float world_strength_hi = 10.0f;
      constexpr float world_strength_lo = 0.0f;
   }

   // Camera (REGISTER_NODE CameraNode)
   namespace Camera
   {
      constexpr float fov_lo = 10.0f;
      constexpr float fov_hi = 120.0f;
      constexpr float ortho_height_lo = 0.2f;
      constexpr float ortho_height_hi = 8.0f;
      constexpr float distance_lo = 0.3f;
      constexpr float distance_hi = 20.0f;
      constexpr float azimuth_lo = -180.0f;
      constexpr float azimuth_hi = 180.0f;
      constexpr float elevation_lo = -85.9437f;
      constexpr float elevation_hi = 85.9437f;
      constexpr float roll_lo = -180.0f;
      constexpr float roll_hi = 180.0f;
      constexpr float target_x_lo = -3.0f;
      constexpr float target_x_hi = 3.0f;
      constexpr float target_y_lo = -3.0f;
      constexpr float target_y_hi = 3.0f;
      constexpr float target_z_lo = -3.0f;
      constexpr float target_z_hi = 3.0f;
      constexpr float near_plane_lo = 0.01f;
      constexpr float near_plane_hi = 1.0f;
      constexpr float far_plane_lo = 5.0f;
      constexpr float far_plane_hi = 500.0f;
      constexpr float distance_2_lo = 0.05f;
      constexpr float distance_2_hi = 100.0f;
      constexpr float elevation_2_lo = -90.0f;
      constexpr float elevation_2_hi = 90.0f;
      constexpr float fov_2_hi = 170.0f;
      constexpr float fov_2_lo = 1.0f;
      constexpr float ortho_height_2_lo = 0.01f;
      constexpr float ortho_height_2_hi = 100.0f;
      constexpr float near_plane_2_lo = 0.001f;
      constexpr float near_plane_2_hi = 10.0f;
      constexpr float far_plane_2_hi = 10000.0f;
      constexpr float far_plane_2_lo = 1.0f;
      constexpr float display_size_lo = 0.01f;
      constexpr float display_size_hi = 10.0f;
   }

   // Light (REGISTER_NODE LightNode)
   namespace Light
   {
      constexpr float azimuth_lo = -180.0f;
      constexpr float azimuth_hi = 180.0f;
      constexpr float elevation_lo = -85.9437f;
      constexpr float elevation_hi = 85.9437f;
      constexpr float distance_lo = 0.2f;
      constexpr float distance_hi = 20.0f;
      constexpr float spot_angle_lo = 5.0f;
      constexpr float spot_angle_hi = 85.0f;
      constexpr float area_width_lo = 0.05f;
      constexpr float area_width_hi = 10.0f;
      constexpr float area_height_lo = 0.05f;
      constexpr float area_height_hi = 10.0f;
      constexpr float area_spread_hi = 180.0f;
      constexpr float area_spread_lo = 1.0f;
      constexpr float temperature_lo = 800.0f;
      constexpr float temperature_hi = 20000.0f;
      constexpr float intensity_hi = 5.0f;
      constexpr float intensity_lo = 0.0f;
      constexpr float exposure_lo = -6.0f;
      constexpr float exposure_hi = 6.0f;
      constexpr float falloff_hi = 4.0f;
      constexpr float falloff_lo = 0.0f;
      constexpr float cutoff_distance_lo = 0.01f;
      constexpr float cutoff_distance_hi = 100.0f;
      constexpr float diffuse_hi = 2.0f;
      constexpr float diffuse_lo = 0.0f;
      constexpr float specular_hi = 2.0f;
      constexpr float specular_lo = 0.0f;
      constexpr float intensity_2_hi = 20.0f;
      constexpr float intensity_2_lo = 0.0f;
      constexpr float exposure_2_lo = -10.0f;
      constexpr float exposure_2_hi = 10.0f;
      constexpr float elevation_2_lo = -90.0f;
      constexpr float elevation_2_hi = 90.0f;
      constexpr float distance_2_lo = 0.05f;
      constexpr float distance_2_hi = 100.0f;
      constexpr float spot_angle_2_hi = 89.0f;
      constexpr float spot_angle_2_lo = 1.0f;
      constexpr float area_width_2_hi = 20.0f;
      constexpr float area_height_2_hi = 20.0f;
      constexpr float transmission_hi = 2.0f;
      constexpr float transmission_lo = 0.0f;
      constexpr float shadow_radius_hi = 5.0f;
      constexpr float shadow_radius_lo = 0.0f;
      constexpr float shadow_filter_hi = 10.0f;
      constexpr float shadow_filter_lo = 0.0f;
      constexpr float shadow_clip_lo = 0.05f;
      constexpr float shadow_clip_hi = 20.0f;
   }

   // HDRI (REGISTER_NODE EnvironmentNode)
   namespace Environment
   {
      constexpr float sky_elevation_hi = 90.0f;
      constexpr float sky_elevation_lo = 0.0f;
      constexpr float sky_azimuth_lo = -180.0f;
      constexpr float sky_azimuth_hi = 180.0f;
      constexpr float turbidity_lo = 1.7f;
      constexpr float turbidity_hi = 10.0f;
      constexpr float intensity_hi = 8.0f;
      constexpr float intensity_lo = 0.0f;
      constexpr float rotation_lo = -180.0f;
      constexpr float rotation_hi = 180.0f;
      constexpr float sun_threshold_hi = 100.0f;
      constexpr float sun_threshold_lo = 0.0f;
   }

   // Render 3D (REGISTER_NODE Render3DNode)
   namespace Render3D
   {
      constexpr int pass_mask_hi = 255;
      constexpr int pass_mask_lo = 0;
      constexpr float mist_start_hi = 100.0f;
      constexpr float mist_start_lo = 0.0f;
      constexpr float mist_depth_lo = 0.01f;
      constexpr float mist_depth_hi = 200.0f;
      constexpr float mist_height_hi = 100.0f;
      constexpr float mist_height_lo = 0.0f;
      constexpr float width_lo = 64.0f;
      constexpr float width_hi = 8192.0f;
      constexpr float height_lo = 64.0f;
      constexpr float height_hi = 8192.0f;
      constexpr float exposure_lo = 0.1f;
      constexpr float exposure_hi = 4.0f;
      constexpr float ortho_height_lo = 0.2f;
      constexpr float ortho_height_hi = 8.0f;
      constexpr float dome_angle_lo = 60.0f;
      constexpr float dome_angle_hi = 360.0f;
      constexpr float fov_lo = 10.0f;
      constexpr float fov_hi = 120.0f;
      constexpr float dome_tilt_hi = 90.0f;
      constexpr float dome_tilt_lo = 0.0f;
      constexpr float lat_min_lo = -90.0f;
      constexpr float lat_min_hi = 90.0f;
      constexpr float lat_max_lo = -90.0f;
      constexpr float lat_max_hi = 90.0f;
      constexpr float lon_min_lo = -180.0f;
      constexpr float lon_min_hi = 180.0f;
      constexpr float lon_max_lo = -180.0f;
      constexpr float lon_max_hi = 180.0f;
      constexpr float cam_distance_lo = 0.3f;
      constexpr float cam_distance_hi = 20.0f;
      constexpr float cam_azimuth_lo = -180.0f;
      constexpr float cam_azimuth_hi = 180.0f;
      constexpr float cam_elevation_lo = -85.9437f;
      constexpr float cam_elevation_hi = 85.9437f;
      constexpr float target_x_lo = -3.0f;
      constexpr float target_x_hi = 3.0f;
      constexpr float target_y_lo = -3.0f;
      constexpr float target_y_hi = 3.0f;
      constexpr float target_z_lo = -3.0f;
      constexpr float target_z_hi = 3.0f;
      constexpr float light_azimuth_lo = -180.0f;
      constexpr float light_azimuth_hi = 180.0f;
      constexpr float light_elevation_lo = -85.9437f;
      constexpr float light_elevation_hi = 85.9437f;
      constexpr float light_intensity_hi = 4.0f;
      constexpr float light_intensity_lo = 0.0f;
      constexpr float rim_intensity_hi = 2.0f;
      constexpr float rim_intensity_lo = 0.0f;
      constexpr float shadow_softness_hi = 4.0f;
      constexpr float shadow_softness_lo = 0.0f;
      constexpr float shadow_bias_lo = 0.0002f;
      constexpr float shadow_bias_hi = 0.02f;
      constexpr float env_intensity_hi = 3.0f;
      constexpr float env_intensity_lo = 0.0f;
      constexpr float ao_radius_lo = 0.05f;
      constexpr float ao_radius_hi = 3.0f;
      constexpr float studio_rotation_lo = -180.0f;
      constexpr float studio_rotation_hi = 180.0f;
      constexpr float cavity_ridge_hi = 2.5f;
      constexpr float cavity_ridge_lo = 0.0f;
      constexpr float cavity_valley_hi = 2.5f;
      constexpr float cavity_valley_lo = 0.0f;
      constexpr float cavity_distance_lo = 0.01f;
      constexpr float cavity_distance_hi = 2.0f;
      constexpr float curvature_ridge_hi = 2.0f;
      constexpr float curvature_ridge_lo = 0.0f;
      constexpr float curvature_valley_hi = 2.0f;
      constexpr float curvature_valley_lo = 0.0f;
      constexpr float fog_density_hi = 4.0f;
      constexpr float fog_density_lo = 0.0f;
      constexpr float fog_anisotropy_lo = -0.95f;
      constexpr float fog_anisotropy_hi = 0.95f;
      constexpr float dof_aperture_hi = 16.0f;
      constexpr float dof_aperture_lo = 0.0f;
      constexpr float focus_distance_lo = 0.3f;
      constexpr float focus_distance_hi = 30.0f;
      constexpr int dof_blades_hi = 16;
      constexpr int dof_blades_lo = 0;
      constexpr float dof_rotation_lo = -180.0f;
      constexpr float dof_rotation_hi = 180.0f;
      constexpr float dof_ratio_lo = 0.01f;
      constexpr float dof_ratio_hi = 2.0f;
      constexpr float lens_distortion_lo = -0.5f;
      constexpr float lens_distortion_hi = 0.5f;
      constexpr float lens_ca_hi = 0.05f;
      constexpr float lens_ca_lo = 0.0f;
      constexpr int supersample_hi = 64;
      constexpr int supersample_lo = 1;
      constexpr float bloom_threshold_lo = 0.5f;
      constexpr float bloom_threshold_hi = 8.0f;
      constexpr float motion_shutter_hi = 2.0f;
      constexpr float motion_shutter_lo = 0.0f;
      constexpr float motion_max_hi = 256.0f;
      constexpr float motion_max_lo = 1.0f;
      constexpr int pt_samples_hi = 4096;
      constexpr int pt_samples_lo = 1;
      constexpr int pt_bounces_hi = 16;
      constexpr int pt_bounces_lo = 1;
      constexpr float focus_distance_2_lo = 0.05f;
      constexpr float focus_distance_2_hi = 100.0f;
   }

   // Draw (REGISTER_NODE DrawNode)
   namespace Draw
   {
      constexpr float brush_size_lo = 0.002f;
      constexpr float brush_size_hi = 0.5f;
      constexpr float opacity_lo = 0.02f;
      constexpr float opacity_hi = 1.0f;
      constexpr float hardness_hi = 0.98f;
      constexpr float hardness_lo = 0.0f;
      constexpr float spacing_lo = 0.02f;
      constexpr float spacing_hi = 1.0f;
      constexpr float jitter_hi = 2.0f;
      constexpr float jitter_lo = 0.0f;
      constexpr float play_speed_lo = 0.1f;
      constexpr float play_speed_hi = 4.0f;
      constexpr float canvas_width_lo = 64.0f;
      constexpr float canvas_width_hi = 4096.0f;
      constexpr float canvas_height_lo = 64.0f;
      constexpr float canvas_height_hi = 4096.0f;
   }

   // Resynthesize (REGISTER_NODE ResynthNode)
   namespace Resynth
   {
      constexpr float steps_per_beat_lo = 0.05f;
      constexpr float steps_per_beat_hi = 16.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Fit (REGISTER_NODE FitNode)
   namespace Fit
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
      constexpr float offset_x_lo = -4096.0f;
      constexpr float offset_x_hi = 4096.0f;
      constexpr float offset_y_lo = -4096.0f;
      constexpr float offset_y_hi = 4096.0f;
   }

   // Scene 2D (REGISTER_NODE Scene2DNode)
   namespace Scene2D
   {
      constexpr float n_lo = 2.0f;
      constexpr float n_hi = 32.0f;
   }

   // Remove Background (REGISTER_NODE RemoveBgNode)
   namespace RemoveBg
   {
      constexpr float feather_hi = 4.0f;
      constexpr float feather_lo = 0.0f;
      constexpr float contrast_lo = 0.5f;
      constexpr float contrast_hi = 8.0f;
      constexpr float refresh_frames_hi = 120.0f;
      constexpr float refresh_frames_lo = 1.0f;
   }

   // Trails (REGISTER_NODE TrailsNode)
   namespace Trails
   {
      constexpr float decay_lo = 0.5f;
      constexpr float decay_hi = 0.999f;
      constexpr float zoom_lo = 0.9f;
      constexpr float zoom_hi = 1.1f;
      constexpr float rotate_lo = -5.7296f;
      constexpr float rotate_hi = 5.7296f;
      constexpr float drift_x_lo = -0.02f;
      constexpr float drift_x_hi = 0.02f;
      constexpr float drift_y_lo = -0.02f;
      constexpr float drift_y_hi = 0.02f;
      constexpr float hue_shift_lo = -0.05f;
      constexpr float hue_shift_hi = 0.05f;
   }

   // Optical Flow (REGISTER_NODE OpticalFlowNode)
   namespace OpticalFlow
   {
      constexpr float gain_hi = 8.0f;
      constexpr float gain_lo = 0.0f;
      constexpr int window_hi = 6;
      constexpr int window_lo = 1;
      constexpr int iterations_hi = 6;
      constexpr int iterations_lo = 1;
   }

   // Time Machine (REGISTER_NODE TimeMachineNode)
   namespace TimeMachine
   {
      constexpr int length_lo = 2;
   }

   // Fluid (REGISTER_NODE FluidNode)
   namespace Fluid
   {
      constexpr float stirrer_strength_hi = 4.0f;
      constexpr float stirrer_strength_lo = 0.0f;
      constexpr float stirrer_size_lo = 0.01f;
      constexpr float stirrer_size_hi = 0.3f;
      constexpr float force_gain_hi = 8.0f;
      constexpr float force_gain_lo = 0.0f;
      constexpr float vorticity_hi = 2.0f;
      constexpr float vorticity_lo = 0.0f;
      constexpr float viscosity_hi = 0.2f;
      constexpr float viscosity_lo = 0.0f;
      constexpr float velocity_damping_hi = 0.1f;
      constexpr float velocity_damping_lo = 0.0f;
      constexpr float dye_fade_hi = 0.1f;
      constexpr float dye_fade_lo = 0.0f;
   }

   // Volume (REGISTER_NODE VolumeNode)
   namespace Volume
   {
      constexpr float density_hi = 8.0f;
      constexpr float density_lo = 0.0f;
      constexpr float size_lo = 0.05f;
      constexpr float size_hi = 6.0f;
      constexpr float height_lo = 0.1f;
      constexpr float height_hi = 10.0f;
      constexpr float center_x_lo = -6.0f;
      constexpr float center_x_hi = 6.0f;
      constexpr float center_y_lo = -6.0f;
      constexpr float center_y_hi = 6.0f;
      constexpr float center_z_lo = -6.0f;
      constexpr float center_z_hi = 6.0f;
      constexpr float noise_scale_lo = 0.1f;
      constexpr float noise_scale_hi = 6.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr float wind_x_lo = -2.0f;
      constexpr float wind_x_hi = 2.0f;
      constexpr float wind_y_lo = -2.0f;
      constexpr float wind_y_hi = 2.0f;
      constexpr float wind_z_lo = -2.0f;
      constexpr float wind_z_hi = 2.0f;
      constexpr float speed_hi = 4.0f;
      constexpr float speed_lo = 0.0f;
      constexpr float light_azimuth_lo = -180.0f;
      constexpr float light_azimuth_hi = 180.0f;
      constexpr float light_elevation_lo = -90.0f;
      constexpr float light_elevation_hi = 90.0f;
      constexpr float light_intensity_hi = 4.0f;
      constexpr float light_intensity_lo = 0.0f;
      constexpr float ambient_hi = 2.0f;
      constexpr float ambient_lo = 0.0f;
      constexpr float anisotropy_lo = -0.9f;
      constexpr float anisotropy_hi = 0.9f;
      constexpr int light_steps_hi = 16;
      constexpr int light_steps_lo = 0;
      constexpr int steps_lo = 8;
      constexpr int steps_hi = 256;
      constexpr int render_steps_lo = 16;
      constexpr int render_steps_hi = 512;
      constexpr float max_distance_hi = 100.0f;
      constexpr float max_distance_lo = 1.0f;
      constexpr float cam_azimuth_lo = -180.0f;
      constexpr float cam_azimuth_hi = 180.0f;
      constexpr float cam_elevation_lo = -89.0f;
      constexpr float cam_elevation_hi = 89.0f;
      constexpr float cam_distance_lo = 0.5f;
      constexpr float cam_distance_hi = 30.0f;
      constexpr float fov_lo = 10.0f;
      constexpr float fov_hi = 120.0f;
   }

   // Reaction Diffusion (REGISTER_NODE ReactionDiffusionNode)
   namespace ReactionDiffusion
   {
      constexpr float feed_lo = 0.01f;
      constexpr float feed_hi = 0.09f;
      constexpr float kill_lo = 0.03f;
      constexpr float kill_hi = 0.08f;
      constexpr float diffuse_a_lo = 0.2f;
      constexpr float diffuse_a_hi = 1.5f;
      constexpr float diffuse_b_lo = 0.1f;
      constexpr float diffuse_b_hi = 1.0f;
      constexpr float steps_per_frame_hi = 32.0f;
      constexpr float steps_per_frame_lo = 1.0f;
      constexpr float width_lo = 64.0f;
      constexpr float width_hi = 2048.0f;
      constexpr float height_lo = 64.0f;
      constexpr float height_hi = 2048.0f;
   }

   // Switcher (REGISTER_NODE SwitcherNode)
   namespace Switcher
   {
      constexpr float interval_lo = 0.05f;
      constexpr float interval_hi = 32.0f;
      constexpr float crossfade_hi = 0.99f;
      constexpr float crossfade_lo = 0.0f;
   }

   // Output (REGISTER_NODE OutputNode)
   namespace Output
   {
      constexpr int record_fps_hi = 60;
      constexpr int record_fps_lo = 1;
   }

   // Projection (REGISTER_NODE ProjectionNode)
   namespace Projection
   {
      constexpr float width_lo = 16.0f;
      constexpr float width_hi = 4096.0f;
      constexpr float height_lo = 16.0f;
      constexpr float height_hi = 4096.0f;
   }

   // LFO (REGISTER_NODE LFONode)
   namespace LFO
   {
      constexpr float seed_hi = 200.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Random (REGISTER_NODE RandomNode)
   namespace Random
   {
      constexpr float seed_hi = 200.0f;
      constexpr float seed_lo = 0.0f;
   }

   // Pattern (REGISTER_NODE PatternNode)
   namespace Pattern
   {
      constexpr float step_beats_lo = 0.05f;
      constexpr float step_beats_hi = 8.0f;
   }

   // Math (REGISTER_NODE MathNode)
   namespace Math
   {
      constexpr float gain_lo = -4.0f;
      constexpr float gain_hi = 4.0f;
   }

   // Compare (REGISTER_NODE CompareNode)
   namespace Compare
   {
      constexpr float tolerance_hi = 0.1f;
      constexpr float tolerance_lo = 0.0f;
   }

   // Logic Gate (REGISTER_NODE LogicGateNode)
   namespace LogicGate
   {
      constexpr float high_thresh_lo = 0.05f;
      constexpr float high_thresh_hi = 0.99f;
      constexpr float low_thresh_lo = 0.01f;
      constexpr float low_thresh_hi = 0.95f;
      constexpr float hold_ms_hi = 1000.0f;
      constexpr float hold_ms_lo = 0.0f;
      constexpr float pulse_ms_lo = 10.0f;
      constexpr float pulse_ms_hi = 500.0f;
      constexpr float refractory_ms_hi = 1000.0f;
      constexpr float refractory_ms_lo = 0.0f;
      constexpr float min_presence_hi = 0.9f;
      constexpr float min_presence_lo = 0.0f;
   }

   // Brain Source (REGISTER_NODE BrainSourceNode)
   namespace BrainSource
   {
      constexpr float hold_ms_hi = 1000.0f;
      constexpr float hold_ms_lo = 0.0f;
   }

   // Range to Range (REGISTER_NODE RangeToRangeNode)
   namespace RangeToRange
   {
      constexpr float in_low_lo = -4.0f;
      constexpr float in_low_hi = 4.0f;
      constexpr float in_high_lo = -4.0f;
      constexpr float in_high_hi = 4.0f;
      constexpr float out_low_lo = -4.0f;
      constexpr float out_low_hi = 4.0f;
      constexpr float out_high_lo = -4.0f;
      constexpr float out_high_hi = 4.0f;
   }

   // Smoothing (REGISTER_NODE SmoothNode)
   namespace Smooth
   {
      constexpr float time_constant_lo = 0.001f;
      constexpr float time_constant_hi = 5.0f;
      constexpr float frequency_lo = 0.1f;
      constexpr float frequency_hi = 20.0f;
      constexpr float damping_hi = 2.0f;
      constexpr float damping_lo = 0.0f;
      constexpr float amount_hi = 0.99f;
      constexpr float amount_lo = 0.0f;
   }

   // Invert (REGISTER_NODE InvertNode)
   namespace Invert
   {
      constexpr float low_lo = -4.0f;
      constexpr float low_hi = 4.0f;
      constexpr float high_lo = -4.0f;
      constexpr float high_hi = 4.0f;
   }

   // MSEG (REGISTER_NODE MSEGNode)
   namespace MSEG
   {
      constexpr float rate_beats_lo = 0.0625f;
      constexpr float rate_beats_hi = 256.0f;
      constexpr float rate_hz_lo = 0.01f;
      constexpr float rate_hz_hi = 40.0f;
      constexpr float length_sec_lo = 0.01f;
      constexpr float length_sec_hi = 600.0f;
      constexpr float length_frames_hi = 100000.0f;
      constexpr float length_frames_lo = 1.0f;
      constexpr float fps_hi = 240.0f;
      constexpr float fps_lo = 1.0f;
      constexpr float speed_lo = 0.25f;
      constexpr float speed_hi = 4.0f;
   }

   // Drift (REGISTER_NODE DriftNode)
   namespace Drift
   {
      constexpr float speed_lo = 0.1f;
      constexpr float speed_hi = 4.0f;
      constexpr float stray_lo = 0.25f;
      constexpr float stray_hi = 4.0f;
      constexpr float momentum_hi = 4.0f;
      constexpr float momentum_lo = 0.0f;
      constexpr float smoothness_hi = 0.95f;
      constexpr float smoothness_lo = 0.0f;
   }

   // Predictive Modulator (REGISTER_NODE PredictiveModulatorNode)
   namespace PredictiveModulator
   {
      constexpr float speed_lo = 0.1f;
      constexpr float speed_hi = 10.0f;
   }

   // Brain Sculpt (REGISTER_NODE BrainSculptNode)
   namespace BrainSculpt
   {
      constexpr float sigma_lo = 0.08f;
      constexpr float sigma_hi = 1.2f;
      constexpr float regularization_hi = 0.5f;
      constexpr float regularization_lo = 0.0f;
   }

   // CV to Pitch (REGISTER_NODE CVToPitchNode)
   namespace CVToPitch
   {
      constexpr int range_low_lo = -48;
      constexpr int range_low_hi = 48;
      constexpr int range_high_lo = -48;
      constexpr int range_high_hi = 48;
      constexpr float glide_ms_hi = 2000.0f;
      constexpr float glide_ms_lo = 0.0f;
   }

   // Macro XY (REGISTER_NODE MacroXYNode)
   namespace MacroXY
   {
      constexpr float speed_lo = 0.05f;
      constexpr float speed_hi = 4.0f;
   }

   // Macro Radio Selector (REGISTER_NODE MacroRadioSelectorNode)
   namespace MacroRadioSelector
   {
      constexpr int count_lo = 2;
      constexpr int count_hi = 8;
   }

   // Macro Step Gate (REGISTER_NODE MacroStepGateNode)
   namespace MacroStepGate
   {
      constexpr float rate_beats_lo = 0.05f;
      constexpr float rate_beats_hi = 4.0f;
   }

   // MIDI Trigger (REGISTER_NODE MidiTriggerNode)
   namespace MidiTrigger
   {
      constexpr float hold_lo = 0.02f;
      constexpr float hold_hi = 2.0f;
   }

   // Path (REGISTER_NODE PathNode)
   namespace Path
   {
      constexpr float speed_lo = -2.0f;
      constexpr float speed_hi = 2.0f;
      constexpr float slice_position_lo = -3.0f;
      constexpr float slice_position_hi = 3.0f;
      constexpr int contour_index_hi = 8;
      constexpr int contour_index_lo = 0;
      constexpr float size_x_hi = 3.0f;
      constexpr float size_x_lo = 0.0f;
      constexpr float size_y_hi = 3.0f;
      constexpr float size_y_lo = 0.0f;
      constexpr float size_z_hi = 3.0f;
      constexpr float size_z_lo = 0.0f;
      constexpr float turns_lo = 0.5f;
      constexpr float turns_hi = 12.0f;
      constexpr int lissajous_a_hi = 9;
      constexpr int lissajous_a_lo = 1;
      constexpr int lissajous_b_hi = 9;
      constexpr int lissajous_b_lo = 1;
   }

   // Geometry Table (REGISTER_NODE GeometryTableNode)
   namespace GeometryTable
   {
      constexpr int rows_hi = 16;
      constexpr int rows_lo = 1;
      constexpr float slice_position_lo = -3.0f;
      constexpr float slice_position_hi = 3.0f;
      constexpr float seed_hi = 32.0f;
      constexpr float seed_lo = 0.0f;
      constexpr float extent_lo = 0.1f;
      constexpr float extent_hi = 10.0f;
   }

   // Image Analyze (REGISTER_NODE ImageAnalyzeNode)
   namespace ImageAnalyze
   {
      constexpr float probe_radius_lo = 0.01f;
      constexpr float probe_radius_hi = 0.5f;
      constexpr float smoothing_hi = 0.99f;
      constexpr float smoothing_lo = 0.0f;
      constexpr float sample_rate_hi = 60.0f;
      constexpr float sample_rate_lo = 1.0f;
      constexpr int sample_size_lo = 8;
      constexpr int sample_size_hi = 256;
      constexpr float gain_hi = 8.0f;
      constexpr float gain_lo = 0.0f;
      constexpr float power_lo = 0.1f;
      constexpr float power_hi = 5.0f;
   }

   // Palette (REGISTER_NODE PaletteNode)
   namespace Palette
   {
      constexpr int swatch_count_lo = 2;
      constexpr float min_chroma_hi = 0.2f;
      constexpr float min_chroma_lo = 0.0f;
      constexpr float seed_hi = 64.0f;
      constexpr float seed_lo = 0.0f;
      constexpr int sample_size_lo = 16;
      constexpr int sample_size_hi = 256;
      constexpr float sample_rate_hi = 60.0f;
      constexpr float sample_rate_lo = 1.0f;
      constexpr float hue_shift_lo = -0.5f;
      constexpr float hue_shift_hi = 0.5f;
      constexpr float saturation_hi = 2.0f;
      constexpr float saturation_lo = 0.0f;
      constexpr float brightness_lo = -0.4f;
      constexpr float brightness_hi = 0.4f;
      constexpr float spread_hi = 2.0f;
      constexpr float spread_lo = 0.0f;
   }

   // Audio File (REGISTER_NODE AudioFileNode)
   namespace AudioFile
   {
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 16.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
   }

   // Audio Analyze (REGISTER_NODE AudioAnalyzeNode)
   namespace AudioAnalyze
   {
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 16.0f;
      constexpr float onset_hold_lo = 0.02f;
      constexpr float onset_hold_hi = 1.0f;
      constexpr float attack_lo = 0.02f;
      constexpr float attack_hi = 1.0f;
      constexpr float release_lo = 0.005f;
      constexpr float release_hi = 1.0f;
   }

   // OSC Receive (REGISTER_NODE OscReceiveNode)
   namespace OscReceive
   {
      constexpr int port_hi = 65535;
      constexpr int port_lo = 1;
   }

   // OSC Send (REGISTER_NODE OscSendNode)
   namespace OscSend
   {
      constexpr int port_hi = 65535;
      constexpr int port_lo = 1;
      constexpr float epsilon_hi = 0.1f;
      constexpr float epsilon_lo = 0.0f;
      constexpr float interval_ms_hi = 1000.0f;
      constexpr float interval_ms_lo = 1.0f;
   }

   // Oscillator (REGISTER_NODE OscillatorNode)
   namespace Oscillator
   {
      constexpr float frequency_lo = 20.0f;
      constexpr float frequency_hi = 8000.0f;
      constexpr float glide_hi = 2.0f;
      constexpr float glide_lo = 0.0f;
      constexpr float pitch_bend_lo = -2.0f;
      constexpr float pitch_bend_hi = 2.0f;
      constexpr float fm_depth_hi = 2.0f;
      constexpr float fm_depth_lo = 0.0f;
   }

   // Wavetable (REGISTER_NODE WavetableNode)
   namespace Wavetable
   {
      constexpr float frequency_lo = 20.0f;
      constexpr float frequency_hi = 8000.0f;
      constexpr float glide_hi = 2.0f;
      constexpr float glide_lo = 0.0f;
      constexpr float pitch_bend_lo = -2.0f;
      constexpr float pitch_bend_hi = 2.0f;
      constexpr float fm_depth_hi = 2.0f;
      constexpr float fm_depth_lo = 0.0f;
   }

   // Analog (REGISTER_NODE AnalogNode)
   namespace Analog
   {
      constexpr float pw1_lo = 0.01f;
      constexpr float pw1_hi = 0.99f;
      constexpr float voices_hi = 7.0f;
      constexpr float voices_lo = 1.0f;
      constexpr float detune_hi = 100.0f;
      constexpr float detune_lo = 0.0f;
      constexpr float cutoff_lo = 20.0f;
      constexpr float cutoff_hi = 18000.0f;
      constexpr float freq_lo = 20.0f;
      constexpr float freq_hi = 8000.0f;
      constexpr float glide_hi = 2.0f;
      constexpr float glide_lo = 0.0f;
      constexpr float pitch_bend_lo = -2.0f;
      constexpr float pitch_bend_hi = 2.0f;
   }

   // Partials (REGISTER_NODE PartialsNode)
   namespace Partials
   {
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float formant_lo = -12.0f;
      constexpr float formant_hi = 12.0f;
      constexpr float partials_hi = 48.0f;
      constexpr float partials_lo = 1.0f;
      constexpr float speed_lo = 0.1f;
      constexpr float speed_hi = 4.0f;
      constexpr float noise_hi = 2.0f;
      constexpr float noise_lo = 0.0f;
      constexpr float release_hi = 5000.0f;
      constexpr float release_lo = 1.0f;
   }

   // String (REGISTER_NODE StringNode)
   namespace String
   {
      constexpr float position_lo = 0.02f;
      constexpr float position_hi = 0.5f;
      constexpr float decay_lo = 0.05f;
      constexpr float decay_hi = 20.0f;
      constexpr float release_hi = 5000.0f;
      constexpr float release_lo = 1.0f;
   }

   // Wind (REGISTER_NODE WindNode)
   namespace Wind
   {
      constexpr float attack_hi = 2000.0f;
      constexpr float attack_lo = 1.0f;
      constexpr float release_hi = 5000.0f;
      constexpr float release_lo = 1.0f;
   }

   // Piano (REGISTER_NODE PianoNode)
   namespace Piano
   {
      constexpr float release_lo = 10.0f;
      constexpr float release_hi = 2000.0f;
   }

   // Wave Terrain (REGISTER_NODE WaveTerrainNode)
   namespace WaveTerrain
   {
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
      constexpr float radius_x_lo = 0.01f;
      constexpr float radius_x_hi = 1.0f;
      constexpr float radius_y_lo = 0.01f;
      constexpr float radius_y_hi = 1.0f;
      constexpr float ratio_a_hi = 16.0f;
      constexpr float ratio_a_lo = 1.0f;
      constexpr float ratio_b_hi = 16.0f;
      constexpr float ratio_b_lo = 1.0f;
      constexpr float rotation_hi = 360.0f;
      constexpr float rotation_lo = 0.0f;
      constexpr float scan_speed_lo = -4.0f;
      constexpr float scan_speed_hi = 4.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
      constexpr float detune_hi = 100.0f;
      constexpr float detune_lo = 0.0f;
      constexpr float cutoff_lo = 20.0f;
      constexpr float cutoff_hi = 20000.0f;
      constexpr float frequency_lo = 20.0f;
      constexpr float frequency_hi = 4000.0f;
      constexpr float glide_hi = 2.0f;
      constexpr float glide_lo = 0.0f;
   }

   // Equation Synth (REGISTER_NODE EquationNode)
   namespace Equation
   {
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
      constexpr float detune_hi = 100.0f;
      constexpr float detune_lo = 0.0f;
      constexpr float cutoff_lo = 20.0f;
      constexpr float cutoff_hi = 20000.0f;
   }

   // Spectral Synth (REGISTER_NODE ImageSpectralSynthNode)
   namespace ImageSpectralSynth
   {
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
      constexpr float scan_speed_lo = 0.05f;
      constexpr float scan_speed_hi = 10.0f;
      constexpr float glide_hi = 2.0f;
      constexpr float glide_lo = 0.0f;
      constexpr float stereo_width_hi = 2.0f;
      constexpr float stereo_width_lo = 0.0f;
      constexpr float min_freq_lo = 20.0f;
      constexpr float min_freq_hi = 2000.0f;
      constexpr float max_freq_lo = 500.0f;
      constexpr float max_freq_hi = 20000.0f;
      constexpr float root_freq_lo = 20.0f;
      constexpr float root_freq_hi = 2000.0f;
      constexpr float brightness_hi = 3.0f;
      constexpr float brightness_lo = 0.0f;
      constexpr float threshold_hi = 0.5f;
      constexpr float threshold_lo = 0.0f;
      constexpr float contrast_lo = 0.2f;
      constexpr float contrast_hi = 4.0f;
      constexpr float detune_hi = 50.0f;
      constexpr float detune_lo = 0.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
      constexpr float cutoff_lo = 20.0f;
      constexpr float cutoff_hi = 20000.0f;
   }

   // Metallic (REGISTER_NODE MetallicNode)
   namespace Metallic
   {
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
      constexpr float transient_lo = 0.05f;
      constexpr float transient_hi = 2.0f;
      constexpr float decay_lo = 0.05f;
      constexpr float decay_hi = 10.0f;
      constexpr float stiffness_hi = 2.0f;
      constexpr float stiffness_lo = 0.0f;
      constexpr float filter_cutoff_lo = 20.0f;
      constexpr float filter_cutoff_hi = 18000.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
      constexpr float frequency_lo = 20.0f;
      constexpr float frequency_hi = 4000.0f;
   }

   // Sampler (REGISTER_NODE SamplerNode)
   namespace Sampler
   {
      constexpr float finetune_lo = -50.0f;
      constexpr float finetune_hi = 50.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float speed_lo = -2.0f;
      constexpr float speed_hi = 2.0f;
      constexpr float decay_lo = 0.05f;
      constexpr float decay_hi = 10.0f;
      constexpr float fade_in_hi = 250.0f;
      constexpr float fade_in_lo = 0.0f;
      constexpr float fade_out_hi = 250.0f;
      constexpr float fade_out_lo = 0.0f;
   }

   // Slicer (REGISTER_NODE SlicerNode)
   namespace Slicer
   {
      constexpr float sensitivity_hi = 100.0f;
      constexpr float sensitivity_lo = 0.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float finetune_lo = -100.0f;
      constexpr float finetune_hi = 100.0f;
      constexpr float attack_hi = 500.0f;
      constexpr float attack_lo = 0.0f;
      constexpr float decay_lo = 5.0f;
      constexpr float decay_hi = 5000.0f;
      constexpr float speed_lo = 0.25f;
      constexpr float speed_hi = 4.0f;
   }

   // PaulStretch (REGISTER_NODE PaulStretchNode)
   namespace PaulStretch
   {
      constexpr float stretch_hi = 1000.0f;
      constexpr float stretch_lo = 1.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
      constexpr float pitch_shift_lo = -24.0f;
      constexpr float pitch_shift_hi = 24.0f;
      constexpr float fine_tune_lo = -100.0f;
      constexpr float fine_tune_hi = 100.0f;
      constexpr float freq_shift_lo = -1000.0f;
      constexpr float freq_shift_hi = 1000.0f;
      constexpr float detune_hi = 100.0f;
      constexpr float detune_lo = 0.0f;
   }

   // Molder (REGISTER_NODE MolderNode)
   namespace Molder
   {
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float level_hi = 2.0f;
      constexpr float level_lo = 0.0f;
   }

   // Grain Molder (REGISTER_NODE GrainMolderNode)
   namespace GrainMolder
   {
      constexpr float grain_lo = 10.0f;
      constexpr float grain_hi = 500.0f;
      constexpr float level_hi = 2.0f;
      constexpr float level_lo = 0.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float decay_lo = 0.05f;
      constexpr float decay_hi = 10.0f;
   }

   // Granular (REGISTER_NODE GranularNode)
   namespace Granular
   {
      constexpr float scan_lo = -4.0f;
      constexpr float scan_hi = 4.0f;
      constexpr float grain_length_lo = 5.0f;
      constexpr float grain_length_hi = 1000.0f;
      constexpr float density_hi = 100.0f;
      constexpr float density_lo = 1.0f;
      constexpr float pitch_shift_lo = -24.0f;
      constexpr float pitch_shift_hi = 24.0f;
      constexpr float random_pitch_hi = 24.0f;
      constexpr float random_pitch_lo = 0.0f;
      constexpr float fine_tune_lo = -100.0f;
      constexpr float fine_tune_hi = 100.0f;
      constexpr float width_hi = 2.0f;
      constexpr float width_lo = 0.0f;
      constexpr float volume_hi = 2.0f;
      constexpr float volume_lo = 0.0f;
   }

   // Drum Sequencer (REGISTER_NODE DrumSequencerNode)
   namespace DrumSequencer
   {
      constexpr float lane_pitch_lo = -24.0f;
      constexpr float lane_pitch_hi = 24.0f;
      constexpr float lane_fine_tune_lo = -50.0f;
      constexpr float lane_fine_tune_hi = 50.0f;
      constexpr float lane_accent_pitch_lo = -24.0f;
      constexpr float lane_accent_pitch_hi = 24.0f;
      constexpr float lane_accent_level_hi = 12.0f;
      constexpr float lane_accent_level_lo = 0.0f;
      constexpr float global_pitch_lo = -24.0f;
      constexpr float global_pitch_hi = 24.0f;
      constexpr float step_nudge_lo = -0.48f;
      constexpr float step_nudge_hi = 0.48f;
   }

   // Looper (REGISTER_NODE LooperNode)
   namespace Looper
   {
      constexpr float finetune_lo = -50.0f;
      constexpr float finetune_hi = 50.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float speed_lo = -2.0f;
      constexpr float speed_hi = 2.0f;
      constexpr float fade_in_hi = 250.0f;
      constexpr float fade_in_lo = 0.0f;
      constexpr float fade_out_hi = 250.0f;
      constexpr float fade_out_lo = 0.0f;
   }

   // Gain (REGISTER_NODE GainNode)
   namespace Gain
   {
      constexpr float gain_db_lo = -60.0f;
      constexpr float gain_db_hi = 12.0f;
   }

   // Mixer (REGISTER_NODE MixerNode)
   namespace Mixer
   {
      constexpr float trim_db_lo = -24.0f;
      constexpr float trim_db_hi = 24.0f;
   }

   // Spatial Mixer (REGISTER_NODE SpatialMixerNode)
   namespace SpatialMixer
   {
      constexpr float azimuth_lo = -180.0f;
      constexpr float azimuth_hi = 180.0f;
      constexpr float elevation_lo = -90.0f;
      constexpr float elevation_hi = 90.0f;
      constexpr float distance_lo = 0.2f;
      constexpr float gain_db_lo = -60.0f;
      constexpr float gain_db_hi = 12.0f;
      constexpr float bass_hz_hi = 300.0f;
      constexpr float bass_hz_lo = 0.0f;
      constexpr float out_db_lo = -60.0f;
      constexpr float out_db_hi = 12.0f;
      constexpr int out_db_def = 2;
   }

   // Audio Texture (REGISTER_NODE AudioTextureNode)
   namespace AudioTexture
   {
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 10.0f;
      constexpr float smoothing_hi = 0.99f;
      constexpr float smoothing_lo = 0.0f;
   }

   // Audio Color Ramp (REGISTER_NODE AudioColorRampNode)
   namespace AudioColorRamp
   {
      constexpr float gain_lo = 0.1f;
      constexpr float gain_hi = 10.0f;
   }

   // MIDI Notes (REGISTER_NODE MidiNotesNode)
   namespace MidiNotes
   {
      constexpr int channel_hi = 15;
      constexpr int channel_lo = -1;
      constexpr int transpose_lo = -24;
      constexpr int transpose_hi = 24;
      constexpr float velocity_scale_hi = 2.0f;
      constexpr float velocity_scale_lo = 0.0f;
      constexpr float mpe_bend_range_hi = 96.0f;
      constexpr float mpe_bend_range_lo = 1.0f;
   }

   // MIDI Out (REGISTER_NODE MidiOutNode)
   namespace MidiOut
   {
      constexpr int channel_hi = 16;
      constexpr int channel_lo = 1;
      constexpr float offset_ms_lo = -50.0f;
      constexpr float offset_ms_hi = 50.0f;
      constexpr int cc_num_hi = 127;
      constexpr int cc_num_lo = 0;
   }

   // Keyboard (REGISTER_NODE KeyboardNode)
   namespace Keyboard
   {
      constexpr int base_octave_hi = 7;
      constexpr int base_octave_lo = -1;
      constexpr int transpose_lo = -24;
      constexpr int transpose_hi = 24;
      constexpr float velocity_scale_hi = 2.0f;
      constexpr float velocity_scale_lo = 0.0f;
   }

   // Note Filter (REGISTER_NODE NoteFilterNode)
   namespace NoteFilter
   {
      constexpr int range_low_hi = 127;
      constexpr int range_low_lo = 0;
      constexpr int range_high_hi = 127;
      constexpr int range_high_lo = 0;
      constexpr float chance_hi = 100.0f;
      constexpr float chance_lo = 0.0f;
   }

   // Velocity Curve (REGISTER_NODE VelocityCurveNode)
   namespace VelocityCurve
   {
      constexpr float curve_lo = 0.25f;
      constexpr float curve_hi = 4.0f;
   }

   // Humanizer (REGISTER_NODE HumanizerNode)
   namespace Humanizer
   {
      constexpr float timing_ms_hi = 100.0f;
      constexpr float timing_ms_lo = 0.0f;
      constexpr float velocity_pct_hi = 100.0f;
      constexpr float velocity_pct_lo = 0.0f;
   }

   // Note Echo (REGISTER_NODE NoteEchoNode)
   namespace NoteEcho
   {
      constexpr float decay_hi = 100.0f;
      constexpr float decay_lo = 0.0f;
   }

   // Predictive Notes (REGISTER_NODE PredictiveNotesNode)
   namespace PredictiveNotes
   {
      constexpr int memory_hi = 8;
      constexpr int memory_lo = 0;
      constexpr int range_low_hi = 127;
      constexpr int range_low_lo = 0;
      constexpr int range_high_hi = 127;
      constexpr int range_high_lo = 0;
   }

   // Note Router (REGISTER_NODE NoteRouterNode)
   namespace NoteRouter
   {
      constexpr float probability_hi = 100.0f;
      constexpr float probability_lo = 0.0f;
   }

   // Arpeggiator (REGISTER_NODE ArpeggiatorNode)
   namespace Arpeggiator
   {
      constexpr int octaves_hi = 4;
      constexpr int octaves_lo = 1;
      constexpr float gate_percent_hi = 100.0f;
      constexpr float gate_percent_lo = 1.0f;
   }

   // Note Sequencer (REGISTER_NODE NoteSequencerNode)
   namespace NoteSequencer
   {
      constexpr float gate_percent_hi = 100.0f;
      constexpr float gate_percent_lo = 1.0f;
   }

   // Random Note Generator (REGISTER_NODE RandomNoteGeneratorNode)
   namespace RandomNoteGenerator
   {
      constexpr int range_low_hi = 127;
      constexpr int range_low_lo = 0;
      constexpr int range_high_hi = 127;
      constexpr int range_high_lo = 0;
      constexpr int max_step_hi = 12;
      constexpr int max_step_lo = 1;
   }

   // MIDI File (REGISTER_NODE MidiFileNode)
   namespace MidiFile
   {
      constexpr int transpose_lo = -48;
      constexpr int transpose_hi = 48;
      constexpr float speed_lo = 0.25f;
      constexpr float speed_hi = 4.0f;
      constexpr float velocity_hi = 2.0f;
      constexpr float velocity_lo = 0.0f;
   }

   // Piano Roll (REGISTER_NODE PianoRollNode)
   namespace PianoRoll
   {
      constexpr int transpose_lo = -48;
      constexpr int transpose_hi = 48;
      constexpr float velocity_hi = 2.0f;
      constexpr float velocity_lo = 0.0f;
   }

   // Chorder (REGISTER_NODE ChorderNode)
   namespace Chorder
   {
      constexpr int chord_size_lo = 2;
      constexpr int chord_size_hi = 6;
      constexpr float upper_harmonics_hi = 100.0f;
      constexpr float upper_harmonics_lo = 0.0f;
      constexpr float strum_ms_hi = 200.0f;
      constexpr float strum_ms_lo = 0.0f;
      constexpr float humanize_timing_ms_hi = 100.0f;
      constexpr float humanize_timing_ms_lo = 0.0f;
      constexpr float humanize_velocity_hi = 100.0f;
      constexpr float humanize_velocity_lo = 0.0f;
   }

   // Bouncing Balls (REGISTER_NODE BouncingBallsNode)
   namespace BouncingBalls
   {
      constexpr float ball_speed_lo = 0.05f;
      constexpr float ball_speed_hi = 2.0f;
      constexpr float ball_size_lo = 0.02f;
      constexpr float ball_size_hi = 0.15f;
      constexpr int range_low_hi = 127;
      constexpr int range_low_lo = 0;
      constexpr int range_high_hi = 127;
      constexpr int range_high_lo = 0;
   }

   // Note Strum (REGISTER_NODE NoteStrumNode)
   namespace NoteStrum
   {
      constexpr float strum_ms_hi = 100.0f;
      constexpr float strum_ms_lo = 0.0f;
   }

   // Envelope (REGISTER_NODE EnvelopeNode)
   namespace Envelope
   {
      constexpr float hold_ms_hi = 4000.0f;
      constexpr float hold_ms_lo = 0.0f;
   }

   // Note to CV (REGISTER_NODE NoteToCVNode)
   namespace NoteToCV
   {
      constexpr int range_low_hi = 126;
      constexpr int range_low_lo = 0;
      constexpr int range_high_hi = 127;
      constexpr int range_high_lo = 1;
      constexpr float glide_ms_hi = 500.0f;
      constexpr float glide_ms_lo = 0.0f;
   }

   // CV Recorder (REGISTER_NODE CVRecorderNode)
   namespace CVRecorder
   {
      constexpr float speed_lo = 0.05f;
      constexpr float speed_hi = 4.0f;
   }

   // Audio to CV (REGISTER_NODE AudioToCVNode)
   namespace AudioToCV
   {
      constexpr float gain_hi = 5.0f;
      constexpr float gain_lo = 0.0f;
      constexpr float attack_ms_lo = 0.1f;
      constexpr float attack_ms_hi = 200.0f;
      constexpr float release_ms_hi = 1000.0f;
      constexpr float release_ms_lo = 1.0f;
   }

   // Panel and settings controls (not node params)
   namespace arrange_session_grid
   {
      constexpr float scene_tempo_edit_speed = 0.25f;
      constexpr float scene_tempo_edit_hi = 300.0f;
      constexpr float scene_tempo_edit_lo = 0.0f;
   }

   namespace audio_node
   {
      constexpr int octave_lo = -4;
      constexpr int octave_hi = 4;
      constexpr int semi_lo = -24;
      constexpr int semi_hi = 24;
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
   }

   namespace curve_editor
   {
      constexpr int seed_hi = 999;
      constexpr int seed_lo = 1;
      constexpr int steps_hi = 64;
      constexpr int steps_lo = 1;
      constexpr float pct_lo = 0.1f;
      constexpr float pct_hi = 10.0f;
   }

   namespace drum
   {
      constexpr float vel_lo = 0.05f;
      constexpr float vel_hi = 1.0f;
   }

   namespace field_params
   {
      constexpr int voices_hi = 16;
      constexpr int voices_lo = 1;
      constexpr int root_hi = 11;
      constexpr int root_lo = 0;
   }

   namespace field_patcher
   {
      constexpr float num_speed = 0.01f;
      constexpr float default_speed = 0.01f;
      constexpr float lo_speed = 0.01f;
      constexpr float hi_speed = 0.01f;
      constexpr float value_speed = 0.01f;
   }

   namespace fx
   {
      constexpr float freq_lo = 20.0f;
      constexpr float freq_hi = 20000.0f;
      constexpr float q_lo = 0.1f;
      constexpr float q_hi = 18.0f;
      constexpr float gain_lo = -24.0f;
      constexpr float gain_hi = 24.0f;
      constexpr float threshold_lo = -60.0f;
      constexpr float threshold_hi = 0.0f;
      constexpr float ratio_hi = 20.0f;
      constexpr float ratio_lo = 1.0f;
      constexpr float attack_lo = 0.05f;
      constexpr float attack_hi = 200.0f;
      constexpr float release_lo = 5.0f;
      constexpr float release_hi = 2000.0f;
      constexpr float makeup_lo = -12.0f;
      constexpr float makeup_hi = 24.0f;
      constexpr float threshold_2_lo = -30.0f;
      constexpr float threshold_2_hi = 0.0f;
      constexpr float release_2_hi = 1000.0f;
      constexpr float in_gain_lo = -12.0f;
      constexpr float in_gain_hi = 24.0f;
      constexpr float out_gain_lo = -24.0f;
      constexpr float out_gain_hi = 12.0f;
      constexpr float time_hi = 2000.0f;
      constexpr float time_lo = 1.0f;
      constexpr float feedback_hi = 110.0f;
      constexpr float feedback_lo = 0.0f;
      constexpr float decay_lo = 0.1f;
      constexpr float decay_hi = 20.0f;
      constexpr float predelay_hi = 500.0f;
      constexpr float predelay_lo = 0.0f;
      constexpr float drive_hi = 40.0f;
      constexpr float drive_lo = 0.0f;
      constexpr float output_lo = -24.0f;
      constexpr float output_hi = 12.0f;
      constexpr float feedback_2_hi = 0.95f;
      constexpr float feedback_2_lo = 0.0f;
      constexpr float drive_2_hi = 24.0f;
      constexpr float drive_2_lo = 0.0f;
      constexpr float width_hi = 2.0f;
      constexpr float width_lo = 0.0f;
      constexpr float bass_mono_hi = 500.0f;
      constexpr float bass_mono_lo = 0.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float grain_lo = 10.0f;
      constexpr float grain_hi = 250.0f;
      constexpr float delay_hi = 30.0f;
      constexpr float delay_lo = 1.0f;
      constexpr float depth_hi = 15.0f;
      constexpr float depth_lo = 0.0f;
      constexpr float feedback_3_hi = 0.9f;
      constexpr float feedback_3_lo = 0.0f;
      constexpr float delay_2_lo = 0.2f;
      constexpr float delay_2_hi = 15.0f;
      constexpr float depth_2_hi = 10.0f;
      constexpr float depth_2_lo = 0.0f;
      constexpr float feedback_4_lo = -0.95f;
      constexpr float feedback_4_hi = 0.95f;
      constexpr float cutoff_lo = 100.0f;
      constexpr float cutoff_hi = 4000.0f;
      constexpr float order_lo = 2.0f;
      constexpr float downsample_lo = 200.0f;
      constexpr float downsample_hi = 44100.0f;
      constexpr float bits_hi = 16.0f;
      constexpr float bits_lo = 1.0f;
      constexpr float attack_2_lo = -24.0f;
      constexpr float attack_2_hi = 24.0f;
      constexpr float sustain_lo = -24.0f;
      constexpr float sustain_hi = 24.0f;
      constexpr float time_2_lo = 20.0f;
      constexpr float time_2_hi = 2000.0f;
      constexpr float steps_lo = 2.0f;
      constexpr float freq_2_hi = 5000.0f;
      constexpr float freq_2_lo = 1.0f;
      constexpr float spread_hi = 100.0f;
      constexpr float spread_lo = 0.0f;
      constexpr float stereo_hi = 180.0f;
      constexpr float stereo_lo = 0.0f;
      constexpr float vowel_hi = 4.0f;
      constexpr float vowel_lo = 0.0f;
      constexpr float q_2_lo = 2.0f;
      constexpr float q_2_hi = 30.0f;
      constexpr float root_lo = 20.0f;
      constexpr float root_hi = 2000.0f;
      constexpr float poles_hi = 16.0f;
      constexpr float poles_lo = 1.0f;
      constexpr float decay_2_lo = 0.05f;
      constexpr float decay_2_hi = 10.0f;
      constexpr float thresh_lo = -60.0f;
      constexpr float thresh_hi = -12.0f;
      constexpr float smooth_hi = 32.0f;
      constexpr float smooth_lo = 0.0f;
      constexpr float blur_lo = 10.0f;
      constexpr float blur_hi = 5000.0f;
      constexpr float glide_hi = 500.0f;
      constexpr float glide_lo = 0.0f;
      constexpr float tune_lo = 20.0f;
      constexpr float tune_hi = 2000.0f;
      constexpr float decay_3_lo = 0.02f;
      constexpr float decay_3_hi = 20.0f;
      constexpr float modes_hi = 32.0f;
      constexpr float modes_lo = 1.0f;
   }

   namespace geometry_params
   {
      constexpr float pose_rot_lo = -180.0f;
      constexpr float pose_rot_hi = 180.0f;
      constexpr int ik_chain_hi = 8;
      constexpr int ik_chain_lo = 1;
      constexpr float ik_target_0_lo = -2.0f;
      constexpr float ik_target_0_hi = 2.0f;
      constexpr float ik_target_1_lo = -2.0f;
      constexpr float ik_target_1_hi = 2.0f;
      constexpr float ik_target_2_lo = -2.0f;
      constexpr float ik_target_2_hi = 2.0f;
      constexpr float ik_pole_0_lo = -2.0f;
      constexpr float ik_pole_0_hi = 2.0f;
      constexpr float ik_pole_1_lo = -2.0f;
      constexpr float ik_pole_1_hi = 2.0f;
      constexpr float ik_pole_2_lo = -2.0f;
      constexpr float ik_pole_2_hi = 2.0f;
      constexpr float vecs_lo = -10.0f;
      constexpr float vecs_hi = 10.0f;
      constexpr int max_points_lo = 16;
      constexpr int max_points_hi = 20000;
      constexpr float point_size_lo = 0.002f;
      constexpr float point_size_hi = 0.3f;
      constexpr float dissolve_angle_degrees_hi = 30.0f;
      constexpr float dissolve_angle_degrees_lo = 0.0f;
      constexpr float normal_amount_lo = -2.0f;
      constexpr float normal_amount_hi = 2.0f;
      constexpr float offset_x_lo = -3.0f;
      constexpr float offset_x_hi = 3.0f;
      constexpr float offset_y_lo = -3.0f;
      constexpr float offset_y_hi = 3.0f;
      constexpr float offset_z_lo = -3.0f;
      constexpr float offset_z_hi = 3.0f;
      constexpr float rot_x_lo = -180.0f;
      constexpr float rot_x_hi = 180.0f;
      constexpr float rot_y_lo = -180.0f;
      constexpr float rot_y_hi = 180.0f;
      constexpr float rot_z_lo = -180.0f;
      constexpr float rot_z_hi = 180.0f;
      constexpr float scale_x_lo = 0.05f;
      constexpr float scale_x_hi = 4.0f;
      constexpr float scale_y_lo = 0.05f;
      constexpr float scale_y_hi = 4.0f;
      constexpr float scale_z_lo = 0.05f;
      constexpr float scale_z_hi = 4.0f;
      constexpr float pivot_x_lo = -3.0f;
      constexpr float pivot_x_hi = 3.0f;
      constexpr float pivot_y_lo = -3.0f;
      constexpr float pivot_y_hi = 3.0f;
      constexpr float pivot_z_lo = -3.0f;
      constexpr float pivot_z_hi = 3.0f;
      constexpr float spin_lo = -2.0f;
      constexpr float spin_hi = 2.0f;
      constexpr int count_hi = 128;
      constexpr int count_lo = 1;
      constexpr float radius_hi = 5.0f;
      constexpr float radius_lo = 0.0f;
      constexpr float offset_x_2_lo = -2.0f;
      constexpr float offset_x_2_hi = 2.0f;
      constexpr float offset_y_2_lo = -2.0f;
      constexpr float offset_y_2_hi = 2.0f;
      constexpr float offset_z_2_lo = -2.0f;
      constexpr float offset_z_2_hi = 2.0f;
      constexpr float rot_step_lo = -85.9437f;
      constexpr float rot_step_hi = 85.9437f;
      constexpr float scale_step_lo = 0.5f;
      constexpr float scale_step_hi = 1.5f;
      constexpr int levels_hi = 3;
      constexpr int levels_lo = 0;
      constexpr float smooth_hi = 2.0f;
      constexpr float smooth_lo = 0.0f;
      constexpr float thickness_lo = 0.0001f;
      constexpr float thickness_hi = 0.5f;
      constexpr float thickness_2_lo = -0.5f;
      constexpr float thickness_2_hi = 0.5f;
      constexpr float inset_hi = 0.9f;
      constexpr float inset_lo = 0.0f;
      constexpr float thickness_3_lo = 0.002f;
      constexpr float thickness_3_hi = 0.1f;
      constexpr float amount_hi = 2.0f;
      constexpr float amount_lo = 0.0f;
      constexpr float amount_2_hi = 3.0f;
      constexpr float amount_2_lo = 0.0f;
      constexpr float seed_hi = 100.0f;
      constexpr float seed_lo = 0.0f;
      constexpr int iterations_hi = 20;
      constexpr int iterations_lo = 1;
      constexpr float mirror_offset_lo = -2.0f;
      constexpr float mirror_offset_hi = 2.0f;
      constexpr float select_a_hi = 2000.0f;
      constexpr float select_a_lo = 0.0f;
      constexpr float select_b_hi = 2000.0f;
      constexpr float select_b_lo = 0.0f;
      constexpr float select_c_hi = 32.0f;
      constexpr float select_c_lo = 1.0f;
      constexpr float select_a_2_lo = -3.0f;
      constexpr float select_a_2_hi = 3.0f;
      constexpr float select_b_2_lo = -3.0f;
      constexpr float select_b_2_hi = 3.0f;
      constexpr float select_seed_hi = 100.0f;
      constexpr float select_seed_lo = 0.0f;
      constexpr float select_c_2_lo = -3.0f;
      constexpr float select_c_2_hi = 3.0f;
      constexpr float select_seed_2_hi = 3.0f;
      constexpr float select_seed_2_lo = 0.0f;
      constexpr float scale_x_2_lo = 0.1f;
      constexpr float scale_x_2_hi = 3.0f;
      constexpr float scale_y_2_lo = 0.1f;
      constexpr float scale_y_2_hi = 3.0f;
      constexpr float scale_z_2_lo = 0.1f;
      constexpr float scale_z_2_hi = 3.0f;
      constexpr float bevel_width_lo = 0.001f;
      constexpr float bevel_width_hi = 0.5f;
      constexpr int bevel_segments_hi = 16;
      constexpr int bevel_segments_lo = 1;
      constexpr float bevel_angle_hi = 120.0f;
      constexpr float bevel_angle_lo = 1.0f;
      constexpr float inset_width_lo = 0.001f;
      constexpr float inset_width_hi = 0.5f;
      constexpr float inset_depth_lo = -0.5f;
      constexpr float inset_depth_hi = 0.5f;
      constexpr float uv_angle_hi = 89.0f;
      constexpr float uv_angle_lo = 1.0f;
      constexpr float uv_margin_hi = 0.2f;
      constexpr float uv_margin_lo = 0.0f;
      constexpr float uv_scale_lo = 0.1f;
      constexpr float uv_scale_hi = 10.0f;
      constexpr float fur_density_hi = 400.0f;
      constexpr float fur_density_lo = 1.0f;
      constexpr int fur_max_strands_lo = 1000;
      constexpr int fur_max_strands_hi = 400000;
      constexpr float fur_length_hi = 0.5f;
      constexpr float fur_length_lo = 0.0f;
      constexpr float fur_width_lo = 0.00005f;
      constexpr float fur_width_hi = 0.005f;
      constexpr int fur_segments_hi = 32;
      constexpr int fur_segments_lo = 1;
      constexpr float fur_comb_0_lo = -2.0f;
      constexpr float fur_comb_0_hi = 2.0f;
      constexpr float fur_comb_1_lo = -2.0f;
      constexpr float fur_comb_1_hi = 2.0f;
      constexpr float fur_comb_2_lo = -2.0f;
      constexpr float fur_comb_2_hi = 2.0f;
      constexpr float decimate_ratio_lo = 0.01f;
      constexpr float decimate_ratio_hi = 1.0f;
      constexpr int screw_steps_lo = 3;
      constexpr int screw_steps_hi = 256;
      constexpr float turns_lo = 0.05f;
      constexpr float turns_hi = 6.0f;
      constexpr float rise_lo = -2.0f;
      constexpr float rise_hi = 2.0f;
      constexpr float radius_offset_hi = 3.0f;
      constexpr float radius_offset_lo = 0.0f;
      constexpr float amount_3_lo = -171.8873f;
      constexpr float amount_3_hi = 171.8873f;
      constexpr int b_count_lo = 2;
      constexpr float fog_min_speed = 0.05f;
      constexpr float fog_min_lo = -1000.0f;
      constexpr float fog_min_hi = 1000.0f;
      constexpr float fog_max_speed = 0.05f;
      constexpr float fog_max_lo = -1000.0f;
      constexpr float fog_max_hi = 1000.0f;
   }

   namespace mixer
   {
      constexpr float length_lo = 1.0f;
      constexpr float length_bars_hi = 256.0f;
      constexpr float length_secs_hi = 600.0f;
      constexpr float tail_hi = 30.0f;
      constexpr float tail_lo = 0.0f;
   }

   namespace note
   {
      constexpr float splitter_body_lo = 20.0f;
      constexpr float splitter_body_hi = 20000.0f;
      constexpr float rate_lo = 0.02f;
      constexpr float rate_hi = 2.0f;
   }

   namespace osc
   {
      constexpr int octave_lo = -4;
      constexpr int semi_lo = -12;
      constexpr float attack_hi = 4000.0f;
      constexpr float attack_lo = 0.0f;
      constexpr float decay_hi = 4000.0f;
      constexpr float decay_lo = 0.0f;
      constexpr float release_hi = 4000.0f;
      constexpr float release_lo = 0.0f;
      constexpr float fine_lo = -50.0f;
      constexpr float fine_hi = 50.0f;
      constexpr float detune_hi = 100.0f;
      constexpr float detune_lo = 0.0f;
      constexpr float cutoff_lo = 20.0f;
      constexpr float cutoff_hi = 18000.0f;
      constexpr float warp_ratio_lo = 0.25f;
      constexpr float warp_ratio_hi = 16.0f;
      constexpr float freq_hi = 8000.0f;
      constexpr float freq_lo = 1.0f;
      constexpr float ratio_lo = 0.5f;
      constexpr float ratio_hi = 16.0f;
   }

   namespace param
   {
      constexpr int gw_lo = 2;
      constexpr int gw_hi = 8;
      constexpr float gw_def = 100.0f;
      constexpr int gh_lo = 2;
      constexpr int gh_hi = 8;
      constexpr float gh_def = 100.0f;
      constexpr float rate_lo = 0.05f;
      constexpr float rate_hi = 32.0f;
      constexpr float rate_hz_lo = 0.01f;
      constexpr float rate_hz_hi = 40.0f;
      constexpr float hold_ms_hi = 1000.0f;
      constexpr float hold_ms_lo = 0.0f;
   }

   namespace piano_roll
   {
      constexpr float swing_hi = 0.75f;
      constexpr float swing_lo = 0.0f;
      constexpr float strum_ticks_hi = 240.0f;
      constexpr float strum_ticks_lo = 0.0f;
      constexpr float humanize_time_hi = 120.0f;
      constexpr float humanize_time_lo = 0.0f;
      constexpr float humanize_vel_hi = 40.0f;
      constexpr float humanize_vel_lo = 0.0f;
      constexpr float note_inspector_hi = 127.0f;
      constexpr float note_inspector_lo = 0.0f;
      constexpr float note_inspector_2_hi = 256.0f;
      constexpr float note_inspector_2_lo = 0.0f;
      constexpr float note_inspector_3_lo = 0.015f;
      constexpr float note_inspector_3_hi = 64.0f;
      constexpr float note_inspector_4_lo = -12.0f;
      constexpr float note_inspector_4_hi = 12.0f;
      constexpr float beats_lo = 0.25f;
      constexpr float beats_hi = 64.0f;
      constexpr float context_menu_hi = 127.0f;
      constexpr float context_menu_lo = 1.0f;
      constexpr float context_menu_2_lo = -12.0f;
      constexpr float context_menu_2_hi = 12.0f;
   }

   namespace sample_studio
   {
      constexpr float gain_db_lo = -60.0f;
      constexpr float gain_db_hi = 12.0f;
      constexpr float pitch_st_lo = -48.0f;
      constexpr float pitch_st_hi = 48.0f;
      constexpr float fine_ct_lo = -100.0f;
      constexpr float fine_ct_hi = 100.0f;
      constexpr float length_ratio_lo = 0.25f;
      constexpr float length_ratio_hi = 4.0f;
      constexpr float fade_in_ms_hi = 500.0f;
      constexpr float fade_in_ms_lo = 0.0f;
      constexpr float fade_out_ms_hi = 500.0f;
      constexpr float fade_out_ms_lo = 0.0f;
      constexpr float pitch_lo = -24.0f;
      constexpr float pitch_hi = 24.0f;
      constexpr float fine_lo = -100.0f;
      constexpr float fine_hi = 100.0f;
      constexpr float speed_lo = 0.25f;
      constexpr float speed_hi = 4.0f;
      constexpr float fade_in_ms_2_hi = 1000.0f;
      constexpr float fade_in_ms_2_lo = 0.0f;
      constexpr float fade_out_ms_2_hi = 1000.0f;
      constexpr float fade_out_ms_2_lo = 0.0f;
      constexpr float gain_db_2_lo = -48.0f;
      constexpr float gain_db_2_hi = 24.0f;
   }

   namespace sampler
   {
      constexpr float register_mpc_pad_floats_lo = 100.0f;
      constexpr float register_mpc_pad_floats_hi = 0.0f;
   }

   namespace settings
   {
      constexpr float opacity_lo = 0.10f;
      constexpr float opacity_hi = 1.00f;
      constexpr float rounding_hi = 24.0f;
      constexpr float rounding_lo = 0.0f;
      constexpr float tint_weight_hi = 0.50f;
      constexpr float tint_weight_lo = 0.0f;
      constexpr float ui_scale_lo = 0.5f;
      constexpr float ui_scale_hi = 2.0f;
      constexpr float grid_snap_lo = 5.0f;
      constexpr float grid_snap_hi = 100.0f;
      constexpr float zoom_sensitivity_lo = 0.05f;
      constexpr float zoom_sensitivity_hi = 1.5f;
      constexpr float minimap_size_lo = 120.0f;
      constexpr float minimap_size_hi = 360.0f;
      constexpr float minimap_opacity_lo = 0.2f;
      constexpr float minimap_opacity_hi = 1.0f;
      constexpr int seconds_lo = 15;
      constexpr int seconds_hi = 300;
      constexpr float mini_size_lo = 40.0f;
      constexpr float mini_size_hi = 160.0f;
      constexpr float gizmo_nav_size_lo = 40.0f;
      constexpr float gizmo_nav_size_hi = 160.0f;
      constexpr float gizmo_size_lo = 40.0f;
      constexpr float gizmo_size_hi = 160.0f;
      constexpr int smooth_time_def = 2;
      constexpr float rot_step_hi = 90.0f;
      constexpr float rot_step_lo = 1.0f;
      constexpr float orbit_sens_lo = 0.1f;
      constexpr float orbit_sens_hi = 5.0f;
   }

   namespace stage_menu_bar
   {
      constexpr float viewport_panel_width_lo = 900.0f;
      constexpr float viewport_panel_height_lo = 800.0f;
      constexpr float mod_matrix_width_lo = 900.0f;
      constexpr float mod_matrix_height_lo = 800.0f;
      constexpr float mixer_panel_width_lo = 1600.0f;
      constexpr float mixer_panel_height_lo = 800.0f;
      constexpr float perf_panel_width_lo = 900.0f;
      constexpr float perf_panel_height_lo = 800.0f;
      constexpr float arrange_panel_height_lo = 800.0f;
      constexpr float session_panel_height_lo = 800.0f;
      constexpr float height_lo = 200.0f;
      constexpr float height_hi = 800.0f;
      constexpr float width_lo = 320.0f;
      constexpr float width_hi = 1600.0f;
      constexpr float height_2_lo = 300.0f;
      constexpr float height_2_hi = 800.0f;
      constexpr float terminal_width_lo = 280.0f;
      constexpr float terminal_width_hi = 1400.0f;
      constexpr float terminal_height_lo = 160.0f;
      constexpr float terminal_height_hi = 800.0f;
      constexpr float curve_editor_height_lo = 220.0f;
      constexpr float curve_editor_height_hi = 800.0f;
      constexpr float field_patcher_height_lo = 220.0f;
      constexpr float field_patcher_height_hi = 800.0f;
   }

   namespace stage_side
   {
      constexpr float show_set_fade_ms_hi = 8000.0f;
      constexpr float show_set_fade_ms_lo = 0.0f;
   }

   namespace view2d
   {
      constexpr float loc_hi = 100.0f;
      constexpr float loc_lo = 0.0f;
      constexpr float q_0_lo = -2.0f;
      constexpr float q_0_hi = 2.0f;
      constexpr float q_1_lo = -2.0f;
      constexpr float q_1_hi = 2.0f;
      constexpr float q_2_lo = -2.0f;
      constexpr float q_2_hi = 2.0f;
      constexpr float p_0_hi = 360.0f;
      constexpr float p_0_lo = 0.0f;
      constexpr float p_2_hi = 360.0f;
      constexpr float p_2_lo = 0.0f;
      constexpr float p_0_2_lo = -180.0f;
      constexpr float p_0_2_hi = 180.0f;
      constexpr float p_1_hi = 2.0f;
      constexpr float p_1_lo = 0.0f;
      constexpr float p_0_3_hi = 2.0f;
      constexpr float p_0_3_lo = 0.0f;
      constexpr float pct_hi = 100.0f;
      constexpr float pct_lo = 1.0f;
      constexpr float sel_mod_px_hi = 100.0f;
      constexpr float sel_mod_px_lo = 1.0f;
      constexpr float depth_hi = 300.0f;
      constexpr float depth_lo = 0.0f;
      constexpr float light_az_lo = -180.0f;
      constexpr float light_az_hi = 180.0f;
      constexpr float light_el_lo = 5.0f;
      constexpr float light_el_hi = 90.0f;
      constexpr float grain_scale_lo = 10.0f;
      constexpr float grain_scale_hi = 800.0f;
      constexpr float grain_depth_hi = 100.0f;
      constexpr float grain_depth_lo = 0.0f;
      constexpr float dual_scale_lo = 5.0f;
      constexpr float dual_scale_hi = 200.0f;
      constexpr float dual_scatter_hi = 200.0f;
      constexpr float dual_scatter_lo = 0.0f;
      constexpr float color_period_lo = 10.0f;
      constexpr float color_period_hi = 2000.0f;
      constexpr float wet_hi = 100.0f;
      constexpr float wet_lo = 0.0f;
      constexpr float refill_hi = 100.0f;
      constexpr float refill_lo = 0.0f;
      constexpr float tol_hi = 100.0f;
      constexpr float tol_lo = 0.0f;
      constexpr float size_hi = 1000.0f;
      constexpr float size_lo = 1.0f;
      constexpr float hard_hi = 100.0f;
      constexpr float hard_lo = 0.0f;
      constexpr float op_hi = 100.0f;
      constexpr float op_lo = 1.0f;
      constexpr float flow_hi = 100.0f;
      constexpr float flow_lo = 1.0f;
      constexpr float source_rotate_lo = -180.0f;
      constexpr float source_rotate_hi = 180.0f;
      constexpr float sc_lo = 10.0f;
      constexpr float sc_hi = 400.0f;
      constexpr float spacing_hi = 200.0f;
      constexpr float spacing_lo = 1.0f;
      constexpr float roundness_lo = 5.0f;
      constexpr float roundness_hi = 100.0f;
      constexpr float angle_lo = -180.0f;
      constexpr float angle_hi = 180.0f;
      constexpr float smoothing_hi = 100.0f;
      constexpr float smoothing_lo = 0.0f;
      constexpr float stab_hi = 32.0f;
      constexpr float stab_lo = 0.0f;
      constexpr float pressure_curve_lo = 0.25f;
      constexpr float pressure_curve_hi = 3.0f;
      constexpr float smooth_hi = 100.0f;
      constexpr float smooth_lo = 0.0f;
      constexpr float jitter_hi = 200.0f;
      constexpr float jitter_lo = 0.0f;
      constexpr float scatter_y_hi = 200.0f;
      constexpr float scatter_y_lo = 0.0f;
      constexpr float taper_in_hi = 1000.0f;
      constexpr float taper_in_lo = 0.0f;
      constexpr float taper_out_hi = 1000.0f;
      constexpr float taper_out_lo = 0.0f;
      constexpr float accumulation_hi = 100.0f;
      constexpr float accumulation_lo = 0.0f;
      constexpr float brush_hue_lo = -180.0f;
      constexpr float brush_hue_hi = 180.0f;
      constexpr float size_jitter_hi = 100.0f;
      constexpr float size_jitter_lo = 0.0f;
      constexpr float opacity_jitter_hi = 100.0f;
      constexpr float opacity_jitter_lo = 0.0f;
      constexpr float hard_jitter_hi = 100.0f;
      constexpr float hard_jitter_lo = 0.0f;
      constexpr float angle_jitter_hi = 100.0f;
      constexpr float angle_jitter_lo = 0.0f;
      constexpr float round_jitter_hi = 100.0f;
      constexpr float round_jitter_lo = 0.0f;
      constexpr float accum_jitter_hi = 100.0f;
      constexpr float accum_jitter_lo = 0.0f;
      constexpr float hue_jitter_hi = 100.0f;
      constexpr float hue_jitter_lo = 0.0f;
      constexpr float sat_jitter_hi = 100.0f;
      constexpr float sat_jitter_lo = 0.0f;
      constexpr float val_jitter_hi = 100.0f;
      constexpr float val_jitter_lo = 0.0f;
      constexpr float tines_hi = 16.0f;
      constexpr float tines_lo = 1.0f;
      constexpr float blemish_size_lo = 2.0f;
      constexpr float blemish_size_hi = 500.0f;
      constexpr float feather_hi = 250.0f;
      constexpr float feather_lo = 0.0f;
      constexpr float marquee_fixed_0_hi = 8192.0f;
      constexpr float marquee_fixed_0_lo = 1.0f;
      constexpr float marquee_fixed_1_hi = 8192.0f;
      constexpr float marquee_fixed_1_lo = 1.0f;
      constexpr float fill_angle_lo = -180.0f;
      constexpr float fill_angle_hi = 180.0f;
      constexpr float width_hi = 200.0f;
      constexpr float width_lo = 0.0f;
      constexpr float dash_hi = 200.0f;
      constexpr float dash_lo = 0.0f;
      constexpr float gap_hi = 200.0f;
      constexpr float gap_lo = 0.0f;
      constexpr float dash_phase_lo = -200.0f;
      constexpr float dash_phase_hi = 200.0f;
      constexpr float miter_hi = 100.0f;
      constexpr float miter_lo = 1.0f;
      constexpr float arrow_size_hi = 20.0f;
      constexpr float arrow_size_lo = 1.0f;
      constexpr float radius_hi = 500.0f;
      constexpr float radius_lo = 0.0f;
      constexpr float pct_2_hi = 50.0f;
      constexpr float pct_2_lo = 0.0f;
      constexpr float r_hi = 500.0f;
      constexpr float r_lo = 0.0f;
      constexpr float sides_lo = 3.0f;
      constexpr float sides_hi = 64.0f;
      constexpr float inner_lo = 0.05f;
      constexpr float inner_hi = 1.0f;
      constexpr float w_hi = 8192.0f;
      constexpr float w_lo = 1.0f;
      constexpr float h_hi = 8192.0f;
      constexpr float h_lo = 1.0f;
      constexpr float size_2_lo = 4.0f;
      constexpr float size_2_hi = 800.0f;
      constexpr float leading_lo = 0.5f;
      constexpr float leading_hi = 3.0f;
      constexpr float tracking_lo = -100.0f;
      constexpr float tracking_hi = 400.0f;
      constexpr float fill_pct_hi = 100.0f;
      constexpr float fill_pct_lo = 0.0f;
      constexpr float blend_gamma_lo = 0.1f;
      constexpr float blend_gamma_hi = 10.0f;
      constexpr float noise_size_lo = 1.0f;
      constexpr float noise_size_hi = 16.0f;
      constexpr float noise_seed_hi = 100.0f;
      constexpr float tex_scale_lo = 1.0f;
      constexpr float tex_scale_hi = 64.0f;
      constexpr float tex_oct_lo = 1.0f;
      constexpr float tex_oct_hi = 8.0f;
      constexpr float tex_detail_lo = 0.1f;
      constexpr float tex_detail_hi = 3.0f;
      constexpr float tex_warp_hi = 2.0f;
      constexpr float vmask_feather_hi = 64.0f;
      constexpr float relief_light_lo = -180.0f;
      constexpr float relief_light_hi = 180.0f;
      constexpr float relief_elev_lo = 5.0f;
      constexpr float relief_elev_hi = 90.0f;
      constexpr float relief_depth_hi = 40.0f;
      constexpr float relief_depth_lo = 0.0f;
      constexpr float font_size_lo = 4.0f;
      constexpr float font_size_hi = 800.0f;
      constexpr float box_w_hi = 8192.0f;
      constexpr float box_w_lo = 0.0f;
      constexpr float outline_w_hi = 50.0f;
      constexpr float outline_w_lo = 0.0f;
      constexpr float offset_x_lo = -400.0f;
      constexpr float offset_x_hi = 400.0f;
      constexpr float base_shift_lo = -400.0f;
      constexpr float base_shift_hi = 400.0f;
      constexpr float text_sx_lo = 0.1f;
      constexpr float text_sx_hi = 4.0f;
      constexpr float text_sy_lo = 0.1f;
      constexpr float text_sy_hi = 4.0f;
      constexpr float indent_l_hi = 1000.0f;
      constexpr float indent_l_lo = 0.0f;
      constexpr float indent_r_hi = 1000.0f;
      constexpr float indent_r_lo = 0.0f;
      constexpr float indent_first_lo = -500.0f;
      constexpr float indent_first_hi = 1000.0f;
      constexpr float space_before_hi = 500.0f;
      constexpr float space_before_lo = 0.0f;
      constexpr float space_after_hi = 500.0f;
      constexpr float space_after_lo = 0.0f;
      constexpr int position_speed = 2;
      constexpr float position_hi = -1e6f;
      constexpr float position_lo = 1.0f;
      constexpr float position_def = 1e6f;
      constexpr int size_speed = 2;
      constexpr int scale_speed = 2;
      constexpr float scale_lo = 0.5f;
      constexpr float scale_hi = 0.1f;
      constexpr float scale_def = 10000.0f;
      constexpr int rotate_speed = 3;
      constexpr float rotate_lo = 0.5f;
      constexpr float rotate_hi = -360.0f;
      constexpr float rotate_def = 360.0f;
      constexpr int orient_speed = 3;
      constexpr float orient_lo = 0.5f;
      constexpr float orient_hi = -360.0f;
      constexpr float orient_def = 360.0f;
      constexpr float angle_speed = 0.5f;
      constexpr float shear_speed = 0.5f;
      constexpr float shear_lo = -80.0f;
      constexpr float shear_hi = 80.0f;
      constexpr float gain_hi = 2.0f;
      constexpr float gain_lo = 0.0f;
      constexpr float dist_hi = 200.0f;
      constexpr float dist_lo = 0.0f;
      constexpr float size_3_hi = 200.0f;
      constexpr float size_3_lo = 0.0f;
      constexpr float size_def = 16384.0f;
      constexpr float grid_size_lo = 2.0f;
      constexpr float grid_size_hi = 4096.0f;
      constexpr int grid_subdiv_hi = 16;
      constexpr int grid_subdiv_lo = 1;
      constexpr float q_hi = 100.0f;
      constexpr float q_lo = 1.0f;
      constexpr float fps_speed = 0.1f;
      constexpr float fps_hi = 240.0f;
      constexpr float fps_lo = 1.0f;
      constexpr float seconds_speed = 0.1f;
      constexpr float seconds_lo = 0.1f;
      constexpr float seconds_hi = 3600.0f;
      constexpr float opacity_lo = 0.01f;
      constexpr float opacity_hi = 1.0f;
      constexpr float flow_2_lo = 0.01f;
      constexpr float flow_2_hi = 1.0f;
   }

   namespace view3d
   {
      constexpr float mm_hi = 300.0f;
      constexpr float mm_lo = 1.0f;
      constexpr float sensor_hi = 100.0f;
      constexpr float sensor_lo = 1.0f;
      constexpr float rotation_lo = -180.0f;
      constexpr float rotation_hi = 180.0f;
      constexpr float scale_lo = 0.01f;
      constexpr float scale_hi = 10.0f;
      constexpr float stop_lo = 0.7f;
      constexpr float stop_hi = 128.0f;
      constexpr float intensity_hi = 10.0f;
      constexpr float intensity_lo = 0.0f;
      constexpr float sun_threshold_hi = 100.0f;
      constexpr float sun_threshold_lo = 0.0f;
      constexpr float sun_angle_hi = 10.0f;
      constexpr float sun_angle_lo = 0.0f;
      constexpr float sun_shadow_filter_hi = 10.0f;
      constexpr float sun_shadow_filter_lo = 0.0f;
      constexpr float filter_size_hi = 4.0f;
      constexpr float filter_size_lo = 0.0f;
      constexpr float dither_hi = 2.0f;
      constexpr float dither_lo = 0.0f;
      constexpr float gamma_lo = 0.2f;
      constexpr float gamma_hi = 3.0f;
      constexpr float percent_hi = 200.0f;
      constexpr float percent_lo = 1.0f;
      constexpr float fps_base_hi = 1.01f;
      constexpr float fps_base_lo = 1.0f;
      constexpr float stamp_size_lo = 6.0f;
      constexpr float stamp_size_hi = 96.0f;
      constexpr float unit_scale_lo = 0.001f;
      constexpr float unit_scale_hi = 1000.0f;
      constexpr float snap_move_lo = 0.01f;
      constexpr float snap_move_hi = 5.0f;
      constexpr float snap_rot_hi = 90.0f;
      constexpr float snap_rot_lo = 1.0f;
      constexpr float snap_scale_lo = 0.01f;
      constexpr float snap_scale_hi = 1.0f;
      constexpr float snap_rot_fine_lo = 0.1f;
      constexpr float snap_rot_fine_hi = 15.0f;
      constexpr float prop_size_lo = 0.05f;
      constexpr float prop_size_hi = 50.0f;
      constexpr float v3dcursor_speed = 0.01f;
      constexpr float v3dcursorrot_speed = 0.5f;
      constexpr float view_lens_lo = 10.0f;
      constexpr float view_lens_hi = 250.0f;
      constexpr float clip_start_lo = 0.001f;
      constexpr float clip_start_hi = 10.0f;
      constexpr float clip_end_lo = 10.0f;
      constexpr float clip_end_hi = 10000.0f;
      constexpr float grid_scale_lo = 0.001f;
      constexpr float grid_scale_hi = 1000.0f;
      constexpr float safe_title_hi = 0.3f;
      constexpr float safe_title_lo = 0.0f;
      constexpr float safe_action_hi = 0.3f;
      constexpr float safe_action_lo = 0.0f;
   }

   namespace vmpc
   {
      constexpr float register_pad_floats_lo = 100.0f;
      constexpr float register_pad_floats_hi = 0.0f;
   }

}
