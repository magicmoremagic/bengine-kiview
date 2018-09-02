#pragma once
#ifndef KIVIEW_PCB_HELPER_HPP_
#define KIVIEW_PCB_HELPER_HPP_

#include "node.hpp"
#include <be/util/keyword_parser.hpp>

//////////////////////////////////////////////////////////////////////////////
enum class node_type {
   ignored,
   n_kicad_pcb,
   n_net,
   n_gr_line,
   n_gr_arc,
   n_gr_circle,
   n_gr_text,
   n_module,
   n_segment,
   n_via,
   n_zone,
   n_at,
   n_start,
   n_end,
   n_center,
   n_xy,
   n_xyz,
   n_size,
   n_rect_delta,
   n_width,
   n_thickness,
   n_min_thickness,
   n_angle,
   n_layer,
   n_layers,
   n_drill,
   n_polygon,
   n_filled_polygon,
   n_effects,
   n_font,
   n_pad,
   n_fp_line,
   n_fp_arc,
   n_fp_circle,
   n_fp_text
};

//////////////////////////////////////////////////////////////////////////////
enum class pad_type {
   unsupported,
   p_smd,
   p_thru_hole,
   p_np_thru_hole,
   p_connect
};

//////////////////////////////////////////////////////////////////////////////
enum class pad_shape {
   unsupported,
   s_circle,
   s_oval,
   s_rect,
   s_trapezoid
};

//////////////////////////////////////////////////////////////////////////////
enum class face_type {
   any,
   both,
   f_front,
   f_back
};

//////////////////////////////////////////////////////////////////////////////
enum class layer_type {
   any,
   other,
   l_copper,
   l_silk,
   l_fab,
   l_court,
   l_cuts,
};

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<node_type>& node_type_parser();

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<pad_type>& pad_type_parser();

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<pad_shape>& pad_shape_parser();

//////////////////////////////////////////////////////////////////////////////
node_type get_node_type(const Node& node);

//////////////////////////////////////////////////////////////////////////////
bool check_layer(const Node& node, face_type face, layer_type layer);

#endif
