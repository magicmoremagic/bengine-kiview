#include "render_layer.hpp"
#include "pcb_helper.hpp"
#include "circle.hpp"
#include "polygon.hpp"
#include <be/util/keyword_parser.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <glm/trigonometric.hpp>
#include <string>

namespace {

be::U32 endcap_segments = 18;
be::U32 pad_segments = 18;
be::U32 arc_segments = 72;
be::U32 zone_segments = 18;

using namespace std::string_view_literals;

//////////////////////////////////////////////////////////////////////////////
glm::mat3 translation(const glm::vec2& v) {
   return glm::mat3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, v.x, v.y, 1.f);
}

//////////////////////////////////////////////////////////////////////////////
glm::mat3 rotation(be::F32 radians) {
   be::F32 s = std::sin(radians);
   be::F32 c = std::cos(radians);
   return glm::mat3(c, s, 0.f, -s, c, 0.f, 0.f, 0.f, 1.f);
}

//////////////////////////////////////////////////////////////////////////////
glm::mat3 scaling(const glm::vec2& v) {
   return glm::mat3(v.x, 0.f, 0.f, 0.f, v.y, 0.f, 0.f, 0.f, 1.f);
}

//////////////////////////////////////////////////////////////////////////////
bool intersection(glm::vec2 s0, glm::vec2 e0, glm::vec2 s1, glm::vec2 e1, glm::vec2& out) {
   glm::vec2 d0 = e0 - s0;
   glm::vec2 d1 = e1 - s1;

   be::F32 cross = d0.x * d1.y - d1.x * d0.y;

   if (cross == 0) {
      return false;
   }

   glm::vec2 ds = s0 - s1;
   be::F32 t = (d1.x * ds.y - d1.y * ds.x) / cross;

   out = s0 + (t * d0);
   return true;
}

//////////////////////////////////////////////////////////////////////////////
void render_triangle(const triangle& t, const glm::mat3& transform, std::vector<triangle>& out) {
   out.push_back(triangle { {
         glm::vec2(transform * glm::vec3(t.v[0], 1.f)),
         glm::vec2(transform * glm::vec3(t.v[1], 1.f)),
         glm::vec2(transform * glm::vec3(t.v[2], 1.f))
   } });
}

//////////////////////////////////////////////////////////////////////////////
void render_triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::mat3& transform, std::vector<triangle>& out) {
   out.push_back(triangle { {
         glm::vec2(transform * glm::vec3(a, 1.f)),
         glm::vec2(transform * glm::vec3(b, 1.f)),
         glm::vec2(transform * glm::vec3(c, 1.f))
   } });
}

//////////////////////////////////////////////////////////////////////////////
void render_endcap(glm::vec2 center, glm::vec2 tangent, const glm::mat3& transform, std::vector<triangle>& out) {
   be::U32 n = 0;
   glm::vec2 root;
   glm::vec2 last;
   discretize_arc(center, tangent, glm::pi<be::F32>(), endcap_segments, [&](glm::vec2 v) {
      if (n >= 2) {
         render_triangle(root, last, v, transform, out);
      } else if (n == 0) {
         root = v;
      }
      last = v;
      ++n;
   });
}

//////////////////////////////////////////////////////////////////////////////
void render_line(glm::vec2 start, glm::vec2 end, be::F32 width, const glm::mat3& transform, std::vector<triangle>& out) {
   if (width > 0) {
      be::F32 half_width = width / 2.f;
      glm::vec2 delta = end - start;
      glm::vec2 normal = glm::normalize(glm::vec2(-delta.y, delta.x)) * half_width;

      render_endcap(start, start + normal, transform, out);
      render_endcap(end, end - normal, transform, out);
      
      render_triangle(start + normal, start - normal, end + normal, transform, out);
      render_triangle(start - normal, end + normal, end - normal, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_zone_endcap(glm::vec2 center, glm::vec2 tangent, const glm::mat3& transform, std::vector<triangle>& out) {
   be::U32 n = 0;
   glm::vec2 root;
   glm::vec2 last;
   discretize_arc(center, tangent, glm::pi<be::F32>(), zone_segments, [&](glm::vec2 v) {
      if (n >= 2) {
         render_triangle(root, last, v, transform, out);
      } else if (n == 0) {
         root = v;
      }
      last = v;
      ++n;
   });
}

//////////////////////////////////////////////////////////////////////////////
void render_zone_line(glm::vec2 start, glm::vec2 end, be::F32 width, const glm::mat3& transform, std::vector<triangle>& out) {
   if (width > 0) {
      be::F32 half_width = width / 2.f;
      glm::vec2 delta = end - start;
      glm::vec2 normal = glm::normalize(glm::vec2(-delta.y, delta.x)) * half_width;

      render_zone_endcap(start, start + normal, transform, out);
      render_zone_endcap(end, end - normal, transform, out);
      
      render_triangle(start + normal, start - normal, end + normal, transform, out);
      render_triangle(start - normal, end + normal, end - normal, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_arc(glm::vec2 center, glm::vec2 tangent, be::F32 degrees, be::F32 width, const glm::mat3& transform, std::vector<triangle>& out) {
   if (width > 0 && degrees != 0) {
      const be::F32 half_width = width / 2.f;

      be::U32 n = 0;
      glm::vec2 first, semifinal, last;
      glm::vec2 offset1, offset2;
      
      discretize_arc(center, tangent, glm::radians(degrees), arc_segments, [&](glm::vec2 v) {
         if (n >= 2) {
            glm::vec2 pd = last - semifinal;
            glm::vec2 pn = glm::normalize(glm::vec2(-pd.y, pd.x)) * half_width;
            
            glm::vec2 nd = v - last;
            glm::vec2 nn = glm::normalize(glm::vec2(-nd.y, nd.x)) * half_width;

            glm::vec2 intersect1, intersect2;
            intersection(semifinal - pn, last - pn, v - nn, last - nn, intersect1);
            intersection(semifinal + pn, last + pn, v + nn, last + nn, intersect2);

            render_triangle(offset2, offset1, intersect2, transform, out);
            render_triangle(offset1, intersect2, intersect1, transform, out);

            offset1 = intersect1;
            offset2 = intersect2;

         } else if (n == 1) {
            glm::vec2 d = v - first;
            glm::vec2 offset = glm::normalize(glm::vec2(-d.y, d.x)) * half_width;
            offset1 = first - offset;
            offset2 = first + offset;
            render_endcap(first, offset2, transform, out);
         } else {
            first = v;
         }

         semifinal = last;
         last = v;
         ++n;
      });

      glm::vec2 pd = last - semifinal;
      glm::vec2 pn = glm::normalize(glm::vec2(-pd.y, pd.x)) * half_width;

      glm::vec2 final_offset1 = last - pn;
      glm::vec2 final_offset2 = last + pn;

      render_triangle(offset2, offset1, final_offset2, transform, out);
      render_triangle(offset1, final_offset2, final_offset1, transform, out);

      render_endcap(last, final_offset1, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_circle(glm::vec2 center, glm::vec2 tangent, be::F32 width, const glm::mat3& transform, std::vector<triangle>& out) {
   if (width > 0) {
      const be::F32 radius = glm::distance(center, tangent);
      const be::F32 omega = glm::two_pi<be::F32>() / arc_segments;
      const be::F32 cho = std::cos(omega / 2.f);
      const be::F32 adj_radius = 2.f * radius / (1.f + cho);
      const be::F32 offset = width / (2.f * cho);
      const be::F32 r1 = adj_radius - offset;
      const be::F32 r2 = adj_radius + offset;

      const glm::vec2 p0 = (tangent - center) * r1 / radius;
      const glm::vec2 p1 = (tangent - center) * r2 / radius;

      const glm::mat2 cob0 = glm::mat2(p0, glm::vec2(-p0.y, p0.x));
      const glm::mat2 cob1 = glm::mat2(p1, glm::vec2(-p1.y, p1.x));

      glm::vec2 last0 = center + p0;
      glm::vec2 last1 = center + p1;

      for (be::U32 s = 1; s <= arc_segments; ++s) {
         const be::F32 theta = omega * s;
         const glm::vec2 cs = glm::vec2(std::cos(theta), std::sin(theta));
         const glm::vec2 q0 = center + cob0 * cs;
         const glm::vec2 q1 = center + cob1 * cs;

         render_triangle(last1, last0, q1, transform, out);
         render_triangle(last0, q1, q0, transform, out);

         last0 = q0;
         last1 = q1;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_gr_line(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 start, end;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_start:
               if (child.size() >= 3) {
                  start.x = (be::F32)child[1].value();
                  start.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  end.x = (be::F32)child[1].value();
                  end.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_line(start, end, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_gr_arc(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 center, tangent;
      be::F32 angle = 0;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_start:
               if (child.size() >= 3) {
                  center.x = (be::F32)child[1].value();
                  center.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  tangent.x = (be::F32)child[1].value();
                  tangent.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_angle:
               if (child.size() >= 2) {
                  angle = (be::F32)child[1].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_arc(center, tangent, angle, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_gr_circle(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 center, tangent;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_center:
               if (child.size() >= 3) {
                  center.x = (be::F32)child[1].value();
                  center.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  tangent.x = (be::F32)child[1].value();
                  tangent.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_circle(center, tangent, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_gr_text(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   // TODO
}

//////////////////////////////////////////////////////////////////////////////
void render_fp_line(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 start, end;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_start:
               if (child.size() >= 3) {
                  start.x = (be::F32)child[1].value();
                  start.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  end.x = (be::F32)child[1].value();
                  end.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_line(start, end, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_fp_arc(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 center, tangent;
      be::F32 angle = 0;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_start:
               if (child.size() >= 3) {
                  center.x = (be::F32)child[1].value();
                  center.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  tangent.x = (be::F32)child[1].value();
                  tangent.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_angle:
               if (child.size() >= 2) {
                  angle = (be::F32)child[1].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_arc(center, tangent, angle, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_fp_circle(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 center, tangent;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_center:
               if (child.size() >= 3) {
                  center.x = (be::F32)child[1].value();
                  center.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  tangent.x = (be::F32)child[1].value();
                  tangent.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_circle(center, tangent, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_fp_text(std::vector<const Node*>& stack, const RenderNodePredicate& pred, be::F32 parent_rot, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   // TODO
}

//////////////////////////////////////////////////////////////////////////////
void render_circle_pad(be::F32 radius, const glm::mat3& transform, std::vector<triangle>& out) {
   be::U32 n = 0;
   glm::vec2 root;
   glm::vec2 last;
   discretize_circle(glm::vec2(), radius, pad_segments, [&](glm::vec2 v) {
      if (n >= 2) {
         render_triangle(root, last, v, transform, out);
      } else if (n == 0) {
         root = v;
      }
      last = v;
      ++n;
   });
}

//////////////////////////////////////////////////////////////////////////////
void render_oval_pad(glm::vec2 radius, const glm::mat3& transform, std::vector<triangle>& out) {
   be::U32 n = 0;
   glm::vec2 root;
   glm::vec2 last;
   discretize_oval(glm::vec2(), radius, pad_segments, [&](glm::vec2 v) {
      if (n >= 2) {
         render_triangle(root, last, v, transform, out);
      } else if (n == 0) {
         root = v;
      }
      last = v;
      ++n;
   });
}

//////////////////////////////////////////////////////////////////////////////
void render_rect_pad(glm::vec2 radius, const glm::mat3& transform, std::vector<triangle>& out) {
   glm::vec2 pts[4] = {
      glm::vec2(-radius.x, radius.y),
      glm::vec2(-radius.x, -radius.y),
      glm::vec2(radius.x, -radius.y),
      glm::vec2(radius.x, radius.y)
   };

   render_triangle(pts[0], pts[1], pts[3], transform, out);
   render_triangle(pts[3], pts[1], pts[2], transform, out);
}

//////////////////////////////////////////////////////////////////////////////
void render_trapezoid_pad(glm::vec2 radius, glm::vec2 rect_delta, const glm::mat3& transform, std::vector<triangle>& out) {
   glm::vec2 pts[4] = {
      glm::vec2(-radius.x - rect_delta.y, radius.y + rect_delta.x),
      glm::vec2(-radius.x + rect_delta.y, -radius.y - rect_delta.x),
      glm::vec2(radius.x - rect_delta.y, -radius.y + rect_delta.x),
      glm::vec2(radius.x + rect_delta.y, radius.y - rect_delta.x)
   };

   render_triangle(pts[0], pts[1], pts[3], transform, out);
   render_triangle(pts[3], pts[1], pts[2], transform, out);
}

//////////////////////////////////////////////////////////////////////////////
void render_drill(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_self) {
      glm::vec2 size;

      if (node.size() >= 2) {
         const Node& n = node[1];
         if (n.type() == Node::node_type::value) {
            size = glm::vec2((be::F32)n.value());
         } else if (node.size() >= 4 && n.text() == "oval"sv) {
            size = glm::vec2((be::F32)node[2].value(), (be::F32)node[3].value());
         }
      }

      if (size.x > 0 && size.y > 0) {
         be::U32 n = 0;
         glm::vec2 root;
         glm::vec2 last;
         discretize_oval(glm::vec2(), size / 2.f, pad_segments, [&](glm::vec2 v) {
            if (n >= 2) {
               render_triangle(root, last, v, transform, out);
            } else if (n == 0) {
               root = v;
            }
            last = v;
            ++n;
         });
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_pad(std::vector<const Node*>& stack, const RenderNodePredicate& pred, be::F32 parent_rot, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   if (!render_self && !render_children) {
      return;
   }
   
   pad_shape shape = pad_shape::unsupported;
   glm::vec2 at, size, rect_delta;
   be::F32 rot = 0;
   const Node* drill = nullptr;

   if (node.size() >= 4) {
      shape = pad_shape_parser().parse(node[3].text());
   }

   for (auto& child : node) {
      switch (get_node_type(child)) {
         case node_type::n_at:
            if (child.size() >= 3) {
               at.x = (be::F32)child[1].value();
               at.y = (be::F32)child[2].value();

               if (child.size() >= 4) {
                  rot = (be::F32)child[3].value();
               }
            }
            break;

         case node_type::n_size:
            if (child.size() >= 2) {
               size.x = (be::F32)child[1].value();

               if (child.size() >= 3) {
                  size.y = (be::F32)child[2].value();
               } else {
                  size.y = size.x;
               }
            }
            break;

         case node_type::n_rect_delta:
            if (child.size() >= 2) {
               rect_delta.x = (be::F32)child[1].value();

               if (child.size() >= 3) {
                  rect_delta.y = (be::F32)child[2].value();
               } else {
                  rect_delta.y = rect_delta.x;
               }
            }
            break;

         case node_type::n_drill:
            drill = &child;
            break;
      }
   }

   

   if (render_self && size.x > 0 && size.y > 0) {
      glm::mat3 child_transform = transform * translation(at) * rotation(-glm::radians(rot - parent_rot));
      switch (shape) {
         case pad_shape::s_circle:
            render_circle_pad(size.x / 2.f, child_transform, out);
            break;
         case pad_shape::s_oval:
            render_oval_pad(size / 2.f, child_transform, out);
            break;
         case pad_shape::s_rect:
            render_rect_pad(size / 2.f, child_transform, out);
            break;
         case pad_shape::s_trapezoid:
            render_trapezoid_pad(size / 2.f, rect_delta / 2.f, child_transform, out);
            break;
      }
   }

   if (render_children && drill) {
      glm::mat3 child_transform = transform * translation(at) * rotation(-glm::radians(rot - parent_rot));
      stack.push_back(drill);
      render_drill(stack, pred, child_transform, out);
      stack.pop_back();
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_module(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   
   if (render_children) {
      glm::vec2 at;
      be::F32 rot = 0;

      auto it = find(node, "at"sv);
      if (it != node.end()) {
         auto& child = *it;
         if (child.size() >= 3) {
            at.x = (be::F32)child[1].value();
            at.y = (be::F32)child[2].value();

            if (child.size() >= 4) {
               rot = (be::F32)child[3].value();
            }
         }
      }

      glm::mat3 child_transform = transform * translation(at) * rotation(-glm::radians(rot));

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_pad:
               stack.push_back(&child);
               render_pad(stack, pred, rot, child_transform, out);
               stack.pop_back();
               break;
            case node_type::n_fp_line:
               stack.push_back(&child);
               render_fp_line(stack, pred, child_transform, out);
               stack.pop_back();
               break;
            case node_type::n_fp_arc:
               stack.push_back(&child);
               render_fp_arc(stack, pred, child_transform, out);
               stack.pop_back();
               break;
            case node_type::n_fp_circle:
               stack.push_back(&child);
               render_fp_circle(stack, pred, child_transform, out);
               stack.pop_back();
               break;
            case node_type::n_fp_text:
               stack.push_back(&child);
               render_fp_text(stack, pred, rot, child_transform, out);
               stack.pop_back();
               break;
         }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_segment(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);

   if (render_self) {
      glm::vec2 start, end;
      be::F32 width = 0;

      for (auto& child : node) {
         switch (get_node_type(child)) {
            case node_type::n_start:
               if (child.size() >= 3) {
                  start.x = (be::F32)child[1].value();
                  start.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_end:
               if (child.size() >= 3) {
                  end.x = (be::F32)child[1].value();
                  end.y = (be::F32)child[2].value();
               }
               break;

            case node_type::n_width:
               if (child.size() >= 2) {
                  width = (be::F32)child[1].value();
               }
               break;
         }
      }

      render_line(start, end, width, transform, out);
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_via(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto [render_self, render_children] = pred(node, stack);
   if (!render_self && !render_children) {
      return;
   }

   glm::vec2 at;
   be::F32 size = 0;
   const Node* drill = nullptr;

   for (auto& child : node) {
      switch (get_node_type(child)) {
         case node_type::n_at:
            if (child.size() >= 3) {
               at.x = (be::F32)child[1].value();
               at.y = (be::F32)child[2].value();
            }
            break;

         case node_type::n_size:
            if (child.size() >= 2) {
               size = (be::F32)child[1].value();
            }
            break;

         case node_type::n_drill:
            drill = &child;
            break;
      }
   }
   
   if (render_self && size > 0) {
      be::U32 n = 0;
      glm::vec2 root;
      glm::vec2 last;
      discretize_circle(at, size / 2.f, pad_segments, [&](glm::vec2 v) {
         if (n >= 2) {
            render_triangle(root, last, v, transform, out);
         } else if (n == 0) {
            root = v;
         }
         last = v;
         ++n;
      });
   }

   if (render_children && drill) {
      glm::mat3 drill_transform = transform * translation(at);
      stack.push_back(drill);
      render_drill(stack, pred, drill_transform, out);
      stack.pop_back();
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_zone(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   const Node& node = *stack.back();
   auto[render_self, render_children] = pred(node, stack);

   if (render_self) {
      be::F32 width = 0;

      auto thickness_it = find(node, "min_thickness"sv);
      if (thickness_it != node.end()) {
         auto& child = *thickness_it;
         if (child.size() >= 2) {
            width = (be::F32)child[1].value();
         }
      }

      for (auto& child : node) {
         if (get_node_type(child) == node_type::n_filled_polygon) {
            auto it = find(child, "pts"sv);
            if (it != child.end()) {
               std::vector<glm::vec2> points;
               for (auto& p : *it) {
                  if (p.size() >= 3 && get_node_type(p) == node_type::n_xy) {
                     points.push_back(glm::vec2((be::F32)p[1].value(), (be::F32)p[2].value()));
                  }
               }
               auto n = out.size();
               std::deque<edge> edges;
               make_dcel(points, edges);
               auto n_edges = edges.size();
               triangulate_polygon(edges, out);

               for (auto oit = out.begin() + n, end = out.end(); oit != end; ++oit) {
                  triangle& tri = *oit;
                  tri.v[0] = glm::vec2(transform * glm::vec3(tri.v[0], 1.f));
                  tri.v[1] = glm::vec2(transform * glm::vec3(tri.v[1], 1.f));
                  tri.v[2] = glm::vec2(transform * glm::vec3(tri.v[2], 1.f));
               }

               if (zone_segments > 0 && width > 0) {
                  for (auto zit = edges.begin(), end = zit + n_edges; zit != end; ++zit) {
                     if (zit->next) {
                        render_zone_line(zit->origin, zit->next->origin, width, transform, out);
                     }
                  }
               }
            }
         }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
void render_root(std::vector<const Node*>& stack, const RenderNodePredicate& pred, const glm::mat3& transform, std::vector<triangle>& out) {
   auto& parser = node_type_parser();
   for (const Node& child : *stack.back()) {
      if (!child.empty()) {
         node_type child_type = parser.parse(child[0].text());
         if (child_type != node_type::ignored) {
            stack.push_back(&child);
            switch (child_type) {
               case node_type::n_kicad_pcb: render_root(stack, pred, transform, out); break;
               case node_type::n_gr_line:   render_gr_line(stack, pred, transform, out); break;
               case node_type::n_gr_arc:    render_gr_arc(stack, pred, transform, out); break;
               case node_type::n_gr_circle: render_gr_circle(stack, pred, transform, out); break;
               case node_type::n_gr_text:   render_gr_text(stack, pred, transform, out); break;
               case node_type::n_module:    render_module(stack, pred, transform, out); break;
               case node_type::n_segment:   render_segment(stack, pred, transform, out); break;
               case node_type::n_via:       render_via(stack, pred, transform, out); break;
               case node_type::n_zone:      render_zone(stack, pred, transform, out); break;
            }
            stack.pop_back();
         }
      }
   }
}

} // ::()

//////////////////////////////////////////////////////////////////////////////
be::U32 pad_segment_density() {
   return pad_segments;
}
//////////////////////////////////////////////////////////////////////////////
void pad_segment_density(be::U32 segments_per_circle) {
   pad_segments = segments_per_circle;
}

//////////////////////////////////////////////////////////////////////////////
be::U32 endcap_segment_density() {
   return endcap_segments;
}
//////////////////////////////////////////////////////////////////////////////
void endcap_segment_density(be::U32 segments_per_circle) {
   endcap_segments = segments_per_circle;
}

//////////////////////////////////////////////////////////////////////////////
be::U32 arc_segment_density() {
   return arc_segments;
}
//////////////////////////////////////////////////////////////////////////////
void arc_segment_density(be::U32 segments_per_circle) {
   arc_segments = segments_per_circle;
}

//////////////////////////////////////////////////////////////////////////////
be::U32 zone_perimeter_endcap_segment_density() {
   return zone_segments;
}
//////////////////////////////////////////////////////////////////////////////
void zone_perimeter_endcap_segment_density(be::U32 segments_per_circle) {
   zone_segments = segments_per_circle;
}

//////////////////////////////////////////////////////////////////////////////
std::vector<triangle> render_layer(const Node& node, const RenderNodePredicate& pred) {
   std::vector<triangle> out;
   std::vector<const Node*> stack;
   stack.push_back(&node);
   render_root(stack, pred, glm::mat3(), out);
   return out;
}
