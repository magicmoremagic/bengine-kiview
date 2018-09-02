#include "pcb_helper.hpp"
#include <string>

using namespace std::string_view_literals;

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<node_type>& node_type_parser() {
   static be::util::ExactKeywordParser<node_type> parser = 
      std::move(be::util::ExactKeywordParser<node_type>(node_type::ignored)
         (node_type::n_kicad_pcb, "kicad_pcb")
         (node_type::n_net, "net")
         (node_type::n_gr_line, "gr_line")
         (node_type::n_gr_arc, "gr_arc")
         (node_type::n_gr_circle, "gr_circle")
         (node_type::n_gr_text, "gr_text")
         (node_type::n_module, "module")
         (node_type::n_segment, "segment")
         (node_type::n_via, "via")
         (node_type::n_zone, "zone")
         (node_type::n_at, "at")
         (node_type::n_start, "start")
         (node_type::n_end, "end")
         (node_type::n_center, "center")
         (node_type::n_xy, "xy")
         (node_type::n_xyz, "xyz")
         (node_type::n_size, "size")
         (node_type::n_rect_delta, "rect_delta")
         (node_type::n_width, "width")
         (node_type::n_thickness, "thickness")
         (node_type::n_min_thickness, "min_thickness")
         (node_type::n_angle, "angle")
         (node_type::n_layer, "layer")
         (node_type::n_layers, "layers")
         (node_type::n_drill, "drill")
         (node_type::n_polygon, "polygon")
         (node_type::n_filled_polygon, "filled_polygon")
         (node_type::n_effects, "effects")
         (node_type::n_font, "font")
         (node_type::n_pad, "pad")
         (node_type::n_fp_line, "fp_line")
         (node_type::n_fp_arc, "fp_arc")
         (node_type::n_fp_circle, "fp_circle")
         (node_type::n_fp_text, "fp_text")
      );

   return parser;
}

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<pad_type>& pad_type_parser() {
   static be::util::ExactKeywordParser<pad_type> parser = 
      std::move(be::util::ExactKeywordParser<pad_type>(pad_type::unsupported)
         (pad_type::p_smd, "smd")
         (pad_type::p_thru_hole, "thru_hole")
         (pad_type::p_np_thru_hole, "np_thru_hole")
         (pad_type::p_connect, "connect")
      );

   return parser;
}

//////////////////////////////////////////////////////////////////////////////
const be::util::ExactKeywordParser<pad_shape>& pad_shape_parser() {
   static be::util::ExactKeywordParser<pad_shape> parser = 
      std::move(be::util::ExactKeywordParser<pad_shape>(pad_shape::unsupported)
         (pad_shape::s_circle, "circle")
         (pad_shape::s_oval, "oval")
         (pad_shape::s_rect, "rect")
         (pad_shape::s_trapezoid, "trapezoid")
      );

   return parser;
}

//////////////////////////////////////////////////////////////////////////////
node_type get_node_type(const Node& node) {
   return node.empty() ? node_type::ignored : node_type_parser().parse(node[0].text());
}

namespace {

//////////////////////////////////////////////////////////////////////////////
const be::util::KeywordParser<layer_type>& layer_type_parser() {
   static be::util::KeywordParser<layer_type> parser = 
      std::move(be::util::KeywordParser<layer_type>(layer_type::other)
         (layer_type::l_copper, "F.Cu", "B.Cu", "*.Cu")
         (layer_type::l_silk, "F.SilkS", "B.SilkS", "*.SilkS")
         (layer_type::l_fab, "F.Fab", "B.Fab", "*.Fab")
         (layer_type::l_court, "F.CrtYd", "B.CrtYd", "*.CrtYd")
         (layer_type::l_cuts, "Edge.Cuts")
      );

   return parser;
}

//////////////////////////////////////////////////////////////////////////////
bool check_face(be::SV text, face_type face) {
   face_type f = face_type::both;

   if (text.size() >= 3 && text[1] == '.') {
      switch (text[0]) {
         case 'F':
         case 'f':
            f = face_type::f_front;
            break;
         case 'B':
         case 'b':
            f = face_type::f_back;
            break;
      }
   }

   return face == f ||
      face == face_type::any ||
      f == face_type::both;
}

//////////////////////////////////////////////////////////////////////////////
bool check_layer(be::SV text, face_type face, layer_type layer) {
   if (!check_face(text, face)) {
      return false;
   }

   if (layer == layer_type::any) {
      return true;
   }

   return layer == layer_type_parser().parse(text);
}

} // ::()

//////////////////////////////////////////////////////////////////////////////
bool check_layer(const Node& node, face_type face, layer_type layer) {
   if (node.empty()) {
      return false;
   }

   for (auto it = node.rbegin(), end = node.rend(); it != end; ++it) {
      auto& child = *it;
      if (child.size() >= 2) {
         if (child[0].text() == "layer"sv) {
            return check_layer(child[1].text(), face, layer);
         } else if (child[0].text() == "layers"sv) {
            for (std::size_t i = 1, s = child.size(); i < s; ++i) {
               if (check_layer(child[i].text(), face, layer)) {
                  return true;
               }
            }
         }
      }
   }

   return false;
}