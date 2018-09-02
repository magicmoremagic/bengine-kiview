#pragma once
#ifndef KIVIEW_CIRCLE_HPP_
#define KIVIEW_CIRCLE_HPP_

#include <be/core/be.hpp>
#include <glm/vec2.hpp>
#include <glm/mat2x2.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

//////////////////////////////////////////////////////////////////////////////
template <typename Consumer>
void discretize_circle(glm::vec2 center, be::F32 radius, be::U32 segments, Consumer&& out) {
   const be::F32 omega = glm::two_pi<be::F32>() / segments;
   const be::F32 adj_radius = 2.f * radius / (1.f + std::cos(omega / 2.f));

   const glm::vec2 first_point = center + glm::vec2(adj_radius, 0);
   out(first_point);
   
   for (be::U32 s = 1; s < segments; ++s) {
      const be::F32 theta = omega * s;
      const glm::vec2 point = center + adj_radius * glm::vec2(std::cos(theta), std::sin(theta));
      out(point);
   }
}

//////////////////////////////////////////////////////////////////////////////
template <typename Consumer>
void discretize_arc(glm::vec2 center, glm::vec2 tangent, be::F32 radians, be::U32 segments_per_circle, Consumer&& out) {
   const be::F32 sign = radians < 0.f ? -1.f : 1.f;
   radians = radians * sign;
   const be::F32 target_omega = glm::two_pi<be::F32>() / segments_per_circle;
   const be::U32 segments = (be::U32)(0.5f + radians / target_omega);
   const be::F32 omega = radians / segments;
   const glm::vec2 tangent_delta = tangent - center;
   const glm::vec2 adj_tangent_delta = 2.f * tangent_delta / (1.f + std::cos(omega / 2.f));
   const glm::mat2 cob = glm::mat2(adj_tangent_delta, glm::vec2(-adj_tangent_delta.y, adj_tangent_delta.x));
   
   out(tangent);
   
   for (be::U32 s = 0; s < segments; ++s) {
      const be::F32 theta = sign * omega * (s + 0.5f);
      const glm::vec2 point = center + cob * glm::vec2(std::cos(theta), std::sin(theta));
      out(point);
   }

   const glm::mat2 edge_cob = glm::mat2(tangent_delta, glm::vec2(-tangent_delta.y, tangent_delta.x));
   const glm::vec2 last_point = center + edge_cob * glm::vec2(cos(sign * radians), sin(sign * radians));
   out(last_point);
}

//////////////////////////////////////////////////////////////////////////////
template <typename Consumer>
void discretize_oval(glm::vec2 center, glm::vec2 radius, be::U32 segments, Consumer&& out) {
   if (radius.x > radius.y) {
      const be::F32 pi = glm::pi<be::F32>();
      const be::F32 offset = radius.x - radius.y;
      glm::vec2 offset_center = glm::vec2(center.x + offset, center.y);
      glm::vec2 tangent = glm::vec2(offset_center.x, offset_center.y - radius.y);
      discretize_arc(offset_center, tangent, pi, segments, out);

      offset_center = glm::vec2(center.x - offset, center.y);
      tangent = glm::vec2(offset_center.x, offset_center.y + radius.y);
      discretize_arc(offset_center, tangent, pi, segments, out);

   } else if (radius.x < radius.y) {
      const be::F32 pi = glm::pi<be::F32>();
      const be::F32 offset = radius.y - radius.x;
      glm::vec2 offset_center = glm::vec2(center.x, center.y + offset);
      glm::vec2 tangent = glm::vec2(offset_center.x + radius.x, offset_center.y);
      discretize_arc(offset_center, tangent, pi, segments, out);

      offset_center = glm::vec2(center.x, center.y - offset);
      tangent = glm::vec2(offset_center.x - radius.x, offset_center.y);
      discretize_arc(offset_center, tangent, pi, segments, out);

   } else {
      discretize_circle(center, radius.x, segments, out);
   }
}

#endif
