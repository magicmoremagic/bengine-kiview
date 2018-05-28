#pragma once
#ifndef KIVIEW_POLYGON_HPP_
#define KIVIEW_POLYGON_HPP_

#include <be/core/be.hpp>
#include <be/core/alg.hpp>
#include <be/core/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <deque>
#include <map>

#include <glm/gtc/type_ptr.hpp>
#include <be/gfx/bgl.hpp>
#include <iostream>

///////////////////////////////////////////////////////////////////////////////
struct triangle {
   be::vec2 v[3];
};

///////////////////////////////////////////////////////////////////////////////
struct edge {
   be::vec2 origin; // edge is leaving this vertex
   edge* prev = nullptr;
   edge* next = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
enum class vertex_type : be::U8 {
   start,
   merge,
   low,
   high,
   split,
   end
};

///////////////////////////////////////////////////////////////////////////////
be::F32 vertex_cos(be::vec2 prev, be::vec2 self, be::vec2 next) {
   be::vec2 ps = self - prev;
   be::vec2 psn = be::vec2(-ps.y, ps.x);
   return glm::dot(psn, next - self);
}

///////////////////////////////////////////////////////////////////////////////
be::F32 vertex_cos(edge* e) {
   return vertex_cos(e->prev->origin, e->origin, e->next->origin);
}

///////////////////////////////////////////////////////////////////////////////
bool is_reflex(be::vec2 prev, be::vec2 self, be::vec2 next) {
   return vertex_cos(prev, self, next) < 0;
}

///////////////////////////////////////////////////////////////////////////////
bool is_reflex(edge* e) {
   return vertex_cos(e->prev->origin, e->origin, e->next->origin) < 0;
}

///////////////////////////////////////////////////////////////////////////////
bool is_convex(be::vec2 prev, be::vec2 self, be::vec2 next) {
   return vertex_cos(prev, self, next) > 0;
}

///////////////////////////////////////////////////////////////////////////////
bool is_convex(edge* e) {
   return vertex_cos(e->prev->origin, e->origin, e->next->origin) > 0;
}

///////////////////////////////////////////////////////////////////////////////
struct VertexComp {
   bool operator()(edge* a, edge* b) const {
      be::F32 delta = a->origin.x - b->origin.x;
      if (delta < 0) {
         return true;
      } else if (delta > 0) {
         return false;
      }

      delta = a->origin.y - b->origin.y;
      if (delta < 0) {
         return true;
      } else if (delta > 0) {
         return false;
      }

      be::F32 acos = vertex_cos(a);
      be::F32 bcos = vertex_cos(b);

      return acos > bcos; // convex before reflex
   }
};

///////////////////////////////////////////////////////////////////////////////
struct ReverseVertexComp {
   bool operator()(edge* a, edge* b) const {
      be::F32 delta = a->origin.x - b->origin.x;
      if (delta > 0) {
         return true;
      } else if (delta < 0) {
         return false;
      }

      delta = a->origin.y - b->origin.y;
      if (delta > 0) {
         return true;
      } else if (delta < 0) {
         return false;
      }

      be::F32 acos = vertex_cos(a);
      be::F32 bcos = vertex_cos(b);

      return acos > bcos; // convex before reflex
   }
};

///////////////////////////////////////////////////////////////////////////////
be::F32 edge_y_at_sweep_x(edge* e, be::F32 x) {
   be::vec2 o = e->origin;
   be::vec2 delta = e->next->origin - o;
   if (delta.x == 0) {
      return e->next->origin.y;
   } else {
      return o.y + delta.y * (x - o.x) / delta.x;
   }
}

///////////////////////////////////////////////////////////////////////////////
struct StatusComp {
   bool operator()(edge* a, edge* b) const {
      be::vec2 ao = a->origin;
      be::vec2 bo = b->origin;

      if (ao.x > bo.x) {
         // calculate b->origin.y for compison at a->origin.x
         return ao.y < edge_y_at_sweep_x(b, ao.x);
      } else {
         // calculate a->origin.y for compison at b->origin.x
         return edge_y_at_sweep_x(a, bo.x) < bo.y;
      }
   }
};

///////////////////////////////////////////////////////////////////////////////
std::deque<edge> make_dcel(const std::vector<be::vec2>& polygon) {
   std::deque<edge> list;

   if (polygon.empty()) {
      return list;
   }

   be::vec2 pv = polygon.back();
   for (auto p : polygon) {
      if (p != pv) {
         list.push_back(edge { p });
         pv = p;
      }
   }

   for (auto it = list.begin(), end = list.end(); it != end; ++it) {
      edge& e = *it;
      if (it == list.begin()) {
         e.prev = &(*(--list.end()));
      } else {
         auto prev = it;
         --prev;
         e.prev = &(*prev);
      }

      auto next = it;
      ++next;
      if (next == end) {
         next = list.begin();
      }
      e.next = &(*next);
   }

   return list;
}

///////////////////////////////////////////////////////////////////////////////
vertex_type get_vertex_type(const edge* e) {
   be::vec2 p = e->prev->origin;
   be::vec2 o = e->origin;
   be::vec2 n = e->next->origin;

   if (p.x < o.x) {
      if (n.x > o.x) {
         return vertex_type::low;
      } else if (is_reflex(p, o, n)) {
         return vertex_type::merge;
      } else if (n.x < o.x) {
         return vertex_type::end;
      } else {
         return vertex_type::low;
      }
   } else if (p.x > o.x) {
      if (n.x < o.x) {
         return vertex_type::high;
      } else if (is_reflex(p, o, n)) {
         return vertex_type::split;
      } else if (n.x > o.x) {
         return vertex_type::start;
      } else {
         return vertex_type::high;
      }
   } else if (is_convex(p, o, n)) {
      return n.x >= o.x ? vertex_type::start : vertex_type::end;
   } else if (n.x == o.x) {
      return p.y > o.y ? vertex_type::high : vertex_type::low;
   } else {
      return n.x >= o.x ? vertex_type::low : vertex_type::high;
   }
}

///////////////////////////////////////////////////////////////////////////////
void triangulate_monotone_polygon(edge* start, std::vector<triangle>& out) {
   enum class chain {
      low,
      high
   };
   struct vertex {
      be::vec2 v;
      chain c;
   };

   std::vector<vertex> verts;

   verts.push_back(vertex { start->origin, chain::low }); // chain designation doesn't matter here so we just pick low arbitrarily

   edge* low = start->next;
   edge* high = start->prev;

   if (high == low) {
      // "polygon" is actually a degenerate triangle
      return;
   } else if (high->origin.x < low->origin.x) {
      verts.push_back(vertex { high->origin, chain::high });
      high = high->prev;
   } else {
      verts.push_back(vertex { low->origin, chain::low });
      low = low->next;
   }

   while (high != low) {
      if (high->origin.x < low->origin.x) {
         if (verts.back().c == chain::low) {
            // different chains
            auto it = verts.begin();
            auto next = it + 1;
            while (next != verts.end()) {
               out.push_back(triangle { { it->v, next->v, high->origin } });
               ++it;
               ++next;
            }
            vertex last = verts.back();
            verts.clear();
            verts.push_back(last);
            verts.push_back(vertex { high->origin, chain::high });
            high = high->prev;
         } else {
            // same chain (high)
            while (verts.size() > 1) {
               auto it = verts.rbegin();
               vertex last = *it++;
               vertex prev = *it;

               if (is_convex(high->origin, last.v, prev.v)) {
                  out.push_back(triangle { { high->origin, last.v, prev.v } });
                  verts.pop_back();
               } else {
                  break;
               }
            }
            verts.push_back(vertex { high->origin, chain::high });
            high = high->prev;
         }
      } else {
         if (verts.back().c == chain::high) {
            // different chains
            auto it = verts.begin();
            auto next = it + 1;
            while (next != verts.end()) {
               out.push_back(triangle { { next->v, it->v, low->origin } });
               ++it;
               ++next;
            }
            vertex last = verts.back();
            verts.clear();
            verts.push_back(last);
            verts.push_back(vertex { low->origin, chain::low });
            low = low->next;
         } else {
            // same chain (low)
            while (verts.size() > 1) {
               auto it = verts.rbegin();
               vertex last = *it++;
               vertex prev = *it;

               if (is_convex(low->origin, prev.v, last.v)) {
                  out.push_back(triangle { { low->origin, prev.v, last.v } });
                  verts.pop_back();
               } else {
                  break;
               }
            }
            verts.push_back(vertex { low->origin, chain::low });
            low = low->next;
         }
      }
   }

   // the end point is on both the high and low chains, but we need to get the winding order right
   if (verts.back().c == chain::high) {
      auto it = verts.begin();
      auto next = it + 1;
      while (next != verts.end()) {
         out.push_back(triangle { { next->v, it->v, low->origin } });
         ++it;
         ++next;
      }
   } else {
      auto it = verts.begin();
      auto next = it + 1;
      while (next != verts.end()) {
         out.push_back(triangle { { it->v, next->v, high->origin } });
         ++it;
         ++next;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
/// \brief  remove the specified edge from the status map without touching any
///         other edges at the same position.
void erase_edge(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
   auto [begin, end] = status.equal_range(e);
   for (; begin != end; ++begin) {
      if (begin->first == e) {
         status.erase(begin);
         return;
      }
   }
   std::cout << "error; failed to erase edge from status; edge not found: "
      << e->origin.x << ", " << e->origin.y << " => "
      << e->next->origin.x << ", " << e->next->origin.y << std::endl;
}

///////////////////////////////////////////////////////////////////////////////
//std::multimap<edge*, edge*, StatusComp>::iterator query_status(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
//   if (status.empty()) {
//      std::cout << "error; empty status for merge vertex at "
//         << e->origin.x << ", " << e->origin.y << std::endl;
//      return status.end();
//   }
//
//   auto upper_bound = status.upper_bound(e);
//   if (upper_bound == status.begin()) {
//      std::cout << "error; no status edge at or below merge vertex y: "
//         << e->origin.x << ", " << e->origin.y << std::endl;
//      return status.end();
//   }
//
//   auto last = upper_bound;
//   --last;
//
//   F32 x = e->origin.x;
//
//
//   F32 last_y = edge_y_at_sweep_x(last->first, x);
//
//   if (last_y == e->origin.y) {
//      // look 
//   }
//
//
//   auto it = last;
//
//
//
//   auto lower_bound = status.lower_bound(last->first);
//   if (lower_bound == last) {
//      // only one match
//      return lower_bound;
//   }
//
//   // multiple matches; need to find the right one by following e->next chain until we reach one of the matched nodes
//   for (edge* d = e->next; d != e; d = d->next) {
//      for (auto m = lower_bound; m != upper_bound; ++m) {
//         if (m->first == d) {
//            return m;
//         }
//      }
//   }
//
//   std::cout << "error; matching status edge not reachable in this polygon: "
//      << e->origin.x << ", " << e->origin.y << std::endl;
//
//   return status.end();
//}



///////////////////////////////////////////////////////////////////////////////
std::multimap<edge*, edge*, StatusComp>::iterator query_status_high(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
   if (status.empty()) {
      std::cout << "error; empty status for high vertex at "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto begin = status.begin();

   auto upper_bound = status.lower_bound(e);
   if (upper_bound == begin) {
      std::cout << "error; no status edge below high vertex y: "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto lower_bound = upper_bound;
   --lower_bound;
   auto last = lower_bound;

   be::F32 x = e->origin.x;
   be::F32 y = edge_y_at_sweep_x(lower_bound->first, x);

   while (lower_bound != begin) {
      --lower_bound;
      if (edge_y_at_sweep_x(lower_bound->first, x) != y) {
         ++lower_bound;
         break;
      }
   }

   if (lower_bound == last) {
      // only one match
      return lower_bound;
   }

   // multiple matches; need to find the right one by following e->next chain until we reach one of the matched nodes
   for (edge* d = e->next; d != e; d = d->next) {
      for (auto m = lower_bound; m != upper_bound; ++m) {
         if (m->first == d) {
            return m;
         }
      }
   }

   std::cout << "error; matching status edge not reachable in this polygon: "
         << e->origin.x << ", " << e->origin.y << std::endl;

   return status.end();
}

///////////////////////////////////////////////////////////////////////////////
std::multimap<edge*, edge*, StatusComp>::iterator query_status_high_rev(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
   if (status.empty()) {
      std::cout << "error; empty status for high vertex at "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto begin = status.begin();

   auto upper_bound = status.lower_bound(e);
   if (upper_bound == begin) {
      std::cout << "error; no status edge below high vertex y: "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto lower_bound = upper_bound;
   --lower_bound;
   auto last = lower_bound;

   be::F32 x = e->origin.x;
   be::F32 y = edge_y_at_sweep_x(lower_bound->first, x);

   while (lower_bound != begin) {
      --lower_bound;
      if (edge_y_at_sweep_x(lower_bound->first, x) != y) {
         ++lower_bound;
         break;
      }
   }

   if (lower_bound == last) {
      // only one match
      return lower_bound;
   }

   // multiple matches; need to find the right one by following e->prev chain until we reach one of the matched nodes
   for (edge* d = e->prev; d != e; d = d->prev) {
      for (auto m = lower_bound; m != upper_bound; ++m) {
         if (m->first == d) {
            return m;
         }
      }
   }

   std::cout << "error; matching status edge not reachable in this polygon: "
      << e->origin.x << ", " << e->origin.y << std::endl;

   return status.end();
}

///////////////////////////////////////////////////////////////////////////////
std::multimap<edge*, edge*, StatusComp>::iterator query_status_merge(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
   if (status.empty()) {
      std::cout << "error; empty status for merge vertex at "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto begin = status.begin();

   auto upper_bound = status.upper_bound(e);
   if (upper_bound == begin) {
      std::cout << "error; no status edge at or below merge vertex y: "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto lower_bound = upper_bound;
   --lower_bound;
   auto last = lower_bound;

   be::F32 x = e->origin.x;
   be::F32 y = edge_y_at_sweep_x(lower_bound->first, x);

   while (lower_bound != begin) {
      --lower_bound;
      if (edge_y_at_sweep_x(lower_bound->first, x) != y) {
         ++lower_bound;
         break;
      }
   }

   if (lower_bound == last) {
      // only one match
      return lower_bound;
   }

   // multiple matches; need to find the right one by following e->next chain until we reach one of the matched nodes
   for (edge* d = e->next; d != e; d = d->next) {
      for (auto m = lower_bound; m != upper_bound; ++m) {
         if (m->first == d) {
            return m;
         }
      }
   }

   std::cout << "error; matching status edge not reachable in this polygon: "
      << e->origin.x << ", " << e->origin.y << std::endl;

   return status.end();
}

///////////////////////////////////////////////////////////////////////////////
std::multimap<edge*, edge*, StatusComp>::iterator query_status_split(std::multimap<edge*, edge*, StatusComp>& status, edge* e) {
   if (status.empty()) {
      std::cout << "error; empty status for split vertex at "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }

   auto begin = status.begin();

   auto upper_bound = status.upper_bound(e);
   if (upper_bound == begin) {
      std::cout << "error; no status edge at or below split vertex y: "
         << e->origin.x << ", " << e->origin.y << std::endl;
      return status.end();
   }
   
   auto lower_bound = upper_bound;
   --lower_bound;
   auto last = lower_bound;

   be::F32 x = e->origin.x;
   be::F32 y = edge_y_at_sweep_x(lower_bound->first, x);

   while (lower_bound != begin) {
      --lower_bound;
      if (edge_y_at_sweep_x(lower_bound->first, x) != y) {
         ++lower_bound;
         break;
      }
   }

   if (lower_bound == last) {
      // only one match
      return lower_bound;
   }

   // multiple matches; need to find the right one by following e->prev chain until we reach one of the matched nodes
   for (edge* d = e->prev; d != e; d = d->prev) {
      for (auto m = lower_bound; m != upper_bound; ++m) {
         if (m->first == d) {
            return m;
         }
      }
   }

   std::cout << "error; matching status edge not reachable in this polygon: "
      << e->origin.x << ", " << e->origin.y << std::endl;

   return status.end();
}

///////////////////////////////////////////////////////////////////////////////
std::vector<triangle> triangulate_polygon(const std::vector<be::vec2>& polygon, int highlight) {
   std::vector<triangle> tris;
   std::deque<edge> edges = make_dcel(polygon);
   std::vector<edge*> events(edges.size());
   std::transform(edges.begin(), edges.end(), events.begin(), [](edge& e) { return &e; });
   std::sort(events.begin(), events.end(), VertexComp());

   std::multimap<edge*, edge*, StatusComp> status;
   std::vector<edge*> half_monotone_polygons;

   using namespace be::gfx::gl;

   std::cout << std::endl << "starting triangulation... " << std::endl;

   int monopoly = 0;

   for (edge* e : events) {
      // TODO get next event (if there is one) and if e0.origin == e1.origin:
      //       if (e0.next.origin == e1.prev.origin):
      //       else if (e0.prev.origin == e1.next.origin):
      //       else
      //          warning "degenerate polygon detected"


      vertex_type type = get_vertex_type(e);
      switch (type) {
         //case vertex_type::start: std::cout << "start\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::end: std::cout << "end\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::high: std::cout << "high\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::low: std::cout << "low\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::merge: std::cout << "merge\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::split: std::cout << "split\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //default:std::cout << "?unknown?\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
      }

      // glPointSize(9.f);
      //glBegin(GL_POINTS);
      //switch (type) {
      //   case vertex_type::start: glColor4f(0.f, 1.f, 0.f, 1.f); break;
      //   case vertex_type::end:   glColor4f(1.f, 0.f, 0.f, 1.f); break;
      //   case vertex_type::merge: glColor4f(1.f, 1.f, 0.f, 1.f); break;
      //   case vertex_type::split: glColor4f(1.f, 0.f, 1.f, 1.f); break;
      //   case vertex_type::low:   glColor4f(0.f, 0.f, 1.f, 1.f); break;
      //   case vertex_type::high:  glColor4f(0.f, 1.f, 1.f, 1.f); break;
      //   default:glColor4f(0.5f, 0.5f, 0.5f, 0.25f); break;
      //}
      //glVertex2fv(glm::value_ptr(e->origin));
      //glEnd();
      switch (type) {
         case vertex_type::start:
            status.emplace(e, e);
            break;
         case vertex_type::end:
            half_monotone_polygons.push_back(e);
            /*
              if (monopoly == highlight) {
                  
                  edge* d = e;
                  do {
                     glBegin(GL_LINES);
                     glColor4f(0, 1, 0.5, 0.5f);
                     glVertex2fv(glm::value_ptr(d->origin));
                     glColor4f(1, 0, 0.5, 0.5f);
                     glVertex2fv(glm::value_ptr(d->next->origin));
                     glEnd();
                     d = d->next;
                  } while (d != e);
                  

                  be::F32 size = 5;
                  d = e;
                  do {
                     switch (get_vertex_type(d)) {
                        case vertex_type::start: size = 11.f; glColor4f(0.f, 1.f, 0.f, 1.f); break;
                        case vertex_type::end:   size = 11.f; glColor4f(1.f, 0.f, 0.f, 1.f); break;
                        case vertex_type::merge: size = 7.f;  glColor4f(1.f, 1.f, 0.f, 1.f); break;
                        case vertex_type::split: size = 7.f;  glColor4f(1.f, 0.f, 1.f, 1.f); break;
                        case vertex_type::low:   size = 7.f;  glColor4f(0.f, 0.f, 1.f, 1.f); break;
                        case vertex_type::high:  size = 7.f;  glColor4f(0.f, 1.f, 1.f, 1.f); break;
                        default: size = 5.f; glColor4f(0.5f, 0.5f, 0.5f, 0.25f); break;
                     }
                     glPointSize(size);
                     glBegin(GL_POINTS);
                     glVertex2fv(glm::value_ptr(d->origin));
                     glEnd();
                     d = d->next;
                  } while (d != e);
                  
               }

            ++monopoly;
            /**/
            erase_edge(status, e->prev);
            break;
         case vertex_type::low:
            erase_edge(status, e->prev);
            status.emplace(e, e);
            break;
         case vertex_type::high: {
            auto it = query_status_high(status, e);
            if (it != status.end()) {
               it->second = e;
            }
            break;
         }
         case vertex_type::merge: {
            erase_edge(status, e->prev);
            auto it = query_status_merge(status, e);
            if (it != status.end()) {
               it->second = e;
            }
            break;
         }
         case vertex_type::split: {
            auto it = query_status_split(status, e);
            if (it != status.end()) {
               edge* helper = it->second;
               
               /*std::cout << "   chose helper " << helper->origin.x << ", " << helper->origin.y << " => "
                  << helper->next->origin.x << ", " << helper->next->origin.y << std::endl
                  << "   for lower edge " << it->first->origin.x << ", " << it->first->origin.y << " => "
                  << it->first->next->origin.x << ", " << it->first->next->origin.y << std::endl;*/

                  /*      edge* t = e;
                  glBegin(GL_LINE_STRIP);
                  glColor4f(1, 0, 0, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  glColor4f(1, 0, 0, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  glColor4f(1, 0, 0, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  glColor4f(1, 0, 0, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  glColor4f(1, 0, 0, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  glColor4f(1, 0, 0, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  glColor4f(1, 0, 0, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  glColor4f(1, 0, 0, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  glColor4f(1, 0, 0, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  glColor4f(1, 0, 0, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  glColor4f(1, 0, 0, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  glColor4f(1, 0, 0, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  glEnd();

                  t = it->first;
                  glBegin(GL_LINE_STRIP);
                  glColor4f(0, 0, 1, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  glColor4f(0, 0, 1, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  glColor4f(0, 0, 1, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  glColor4f(0, 0, 1, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  glColor4f(0, 0, 1, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  glColor4f(0, 0, 1, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  glColor4f(0, 0, 1, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  glColor4f(0, 0, 1, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  glColor4f(0, 0, 1, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  glColor4f(0, 0, 1, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  glColor4f(0, 0, 1, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  glColor4f(0, 0, 1, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  glEnd();

                  t = it->second;
                  glBegin(GL_LINE_STRIP);
                  glColor4f(0, 1, 0, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  glColor4f(0, 1, 0, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  glColor4f(0, 1, 0, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  glColor4f(0, 1, 0, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  glColor4f(0, 1, 0, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  glColor4f(0, 1, 0, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  glColor4f(0, 1, 0, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  glColor4f(0, 1, 0, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  glColor4f(0, 1, 0, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  glColor4f(0, 1, 0, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  glColor4f(0, 1, 0, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  glColor4f(0, 1, 0, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  glEnd();*/

               edges.push_back(edge { helper->origin, helper->prev, e });
               edge* hprime = &edges.back();

               edges.push_back(edge { e->origin, e->prev, helper });
               edge* eprime = &edges.back();

               hprime->prev->next = hprime;
               hprime->next->prev = hprime;

               eprime->prev->next = eprime;
               eprime->next->prev = eprime;

               it->second = eprime;
            }
            status.emplace(e, e);
            break;
         }
      }
   }

   std::cout << std::endl << "starting second pass..." << std::endl;

   for (edge* half_monotone_polygon : half_monotone_polygons) {
      status.clear();
      events.clear();
      {
         edge* e = half_monotone_polygon;
         do {
            events.push_back(e);
            e = e->next;
         } while (e != half_monotone_polygon);
      }

//      std::cout << std::endl << "starting half monotone..." << std::endl;

      std::sort(events.begin(), events.end(), ReverseVertexComp());
      for (edge* e : events) {
         vertex_type type = get_vertex_type(e);
               switch (type) {
         //case vertex_type::start: std::cout << "start\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::end: std::cout << "end\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::high: std::cout << "high\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::low: std::cout << "low\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::merge: std::cout << "merge\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //case vertex_type::split: std::cout << "split\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         //default:std::cout << "?unknown?\t" << e->origin.x << ", " << e->origin.y << std::endl; break;
         }

         switch (type) {
            case vertex_type::end: // start
               status.emplace(e->prev, e);
               break;
            case vertex_type::start: // end
               erase_edge(status, e);

               /*
               if (monopoly == highlight) {
                  
                  edge* d = e;
                  do {
                     glBegin(GL_LINES);
                     glColor4f(0, 1, 0.5, 0.5f);
                     glVertex2fv(glm::value_ptr(d->origin));
                     glColor4f(1, 0, 0.5, 0.5f);
                     glVertex2fv(glm::value_ptr(d->next->origin));
                     glEnd();
                     d = d->next;
                  } while (d != e);
                  

                  glPointSize(9.f);
                  glBegin(GL_POINTS);
                  d = e;
                  do {
                     switch (get_vertex_type(d)) {
                        case vertex_type::start: glColor4f(0.f, 1.f, 0.f, 1.f); break;
                        case vertex_type::end:   glColor4f(1.f, 0.f, 0.f, 1.f); break;
                        case vertex_type::merge: glColor4f(1.f, 1.f, 0.f, 1.f); break;
                        case vertex_type::split: glColor4f(1.f, 0.f, 1.f, 1.f); break;
                        case vertex_type::low:   glColor4f(0.f, 0.f, 1.f, 1.f); break;
                        case vertex_type::high:  glColor4f(0.f, 1.f, 1.f, 1.f); break;
                        default:glColor4f(0.5f, 0.5f, 0.5f, 0.25f); break;
                     }
                     glVertex2fv(glm::value_ptr(d->origin));
                     d = d->next;
                  } while (d != e);
                  glEnd();


               }
               ++monopoly;
               */

               triangulate_monotone_polygon(e, tris);
               
               break;
            case vertex_type::low:
               erase_edge(status, e);
               status.emplace(e->prev, e);
               break;
            case vertex_type::high: {
               auto it = query_status_high_rev(status, e);
               if (it != status.end()) {
                  it->second = e;
               }
               break;
            }
            case vertex_type::split: { // merge
               erase_edge(status, e);
               auto it = query_status_split(status, e);
               if (it != status.end()) {
                  it->second = e;
               }
               break;
            }
            case vertex_type::merge: { // split
               auto it = query_status_merge(status, e);
               if (it != status.end()) {
                  edge* helper = it->second;


                  //edge* t = e;
                  //glBegin(GL_LINE_STRIP);
                  //glColor4f(1, 0, 0, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  //glColor4f(1, 0, 0, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  //glColor4f(1, 0, 0, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  //glColor4f(1, 0, 0, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  //glColor4f(1, 0, 0, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  //glColor4f(1, 0, 0, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  //glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  //glColor4f(1, 0, 0, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  //glColor4f(1, 0, 0, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  //glColor4f(1, 0, 0, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  //glColor4f(1, 0, 0, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  //glColor4f(1, 0, 0, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  //glColor4f(1, 0, 0, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  //glEnd();

                  //t = it->first;
                  //glBegin(GL_LINE_STRIP);
                  //glColor4f(0, 0, 1, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  //glColor4f(0, 0, 1, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  //glColor4f(0, 0, 1, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  //glColor4f(0, 0, 1, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  //glColor4f(0, 0, 1, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  //glColor4f(0, 0, 1, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  //glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  //glColor4f(0, 0, 1, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  //glColor4f(0, 0, 1, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  //glColor4f(0, 0, 1, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  //glColor4f(0, 0, 1, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  //glColor4f(0, 0, 1, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  //glColor4f(0, 0, 1, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  //glEnd();

                  //t = it->second;
                  //glBegin(GL_LINE_STRIP);
                  //glColor4f(0, 1, 0, 0.0f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->prev->origin));
                  //glColor4f(0, 1, 0, 0.05f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->prev->origin));
                  //glColor4f(0, 1, 0, 0.15f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->prev->origin));
                  //glColor4f(0, 1, 0, 0.25f); glVertex2fv(glm::value_ptr(t->prev->prev->prev->origin));
                  //glColor4f(0, 1, 0, 0.5f); glVertex2fv(glm::value_ptr(t->prev->prev->origin));
                  //glColor4f(0, 1, 0, 0.75f); glVertex2fv(glm::value_ptr(t->prev->origin));
                  //glColor4f(1, 1, 1, 1.0f); glVertex2fv(glm::value_ptr(t->origin));
                  //glColor4f(0, 1, 0, 0.75f); glVertex2fv(glm::value_ptr(t->next->origin));
                  //glColor4f(0, 1, 0, 0.5f); glVertex2fv(glm::value_ptr(t->next->next->origin));
                  //glColor4f(0, 1, 0, 0.25f); glVertex2fv(glm::value_ptr(t->next->next->next->origin));
                  //glColor4f(0, 1, 0, 0.15f); glVertex2fv(glm::value_ptr(t->next->next->next->next->origin));
                  //glColor4f(0, 1, 0, 0.05f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->origin));
                  //glColor4f(0, 1, 0, 0.0f); glVertex2fv(glm::value_ptr(t->next->next->next->next->next->next->origin));
                  //glEnd();


                  //glBegin(GL_LINE_STRIP);
                  //   //glColor4f(0, 0, 0, 0.5f);
                  //   //glVertex2fv(glm::value_ptr(helper->prev->origin));
                  //   glColor4f(0.f, 0, 0.66f, 0.5f);
                  //   glVertex2fv(glm::value_ptr(it->first->origin));
                  //   glColor4f(0, 0, 0, 0.5f);
                  //   glVertex2fv(glm::value_ptr(it->first->next->origin));
                  //   glEnd();

                  //  glBegin(GL_LINE_STRIP);
                  //   glColor4f(0, 0, 0, 0.5f);
                  //   glVertex2fv(glm::value_ptr(helper->prev->origin));
                  //   glColor4f(0.66f, 0, 0, 0.5f);
                  //   glVertex2fv(glm::value_ptr(helper->origin));
                  //   glColor4f(0, 0, 0, 0.5f);
                  //   glVertex2fv(glm::value_ptr(helper->next->origin));
                  //   glEnd();

                  edges.push_back(edge { helper->origin, helper->prev, e });
                  edge* hprime = &edges.back();

                  edges.push_back(edge { e->origin, e->prev, helper });
                  edge* eprime = &edges.back();

                  hprime->prev->next = hprime;
                  hprime->next->prev = hprime;

                  eprime->prev->next = eprime;
                  eprime->next->prev = eprime;

                  //glBegin(GL_LINE_STRIP);
                    // glColor4f(0.33f, 0, 0, 0.5f);
                     //glVertex2fv(glm::value_ptr(hprime->prev->origin));
                     //glColor4f(0.66f, 0, 0, 0.5f);
                     //glVertex2fv(glm::value_ptr(hprime->origin));
                     //glColor4f(1, 0, 0, 0.5f);
                     //glVertex2fv(glm::value_ptr(hprime->next->origin));
                     //glEnd();

                   /*  glBegin(GL_LINE_STRIP);
                     glColor4f(1, 0.33f, 0, 0.5f);
                     glVertex2fv(glm::value_ptr(eprime->prev->origin));
                     glColor4f(0, 0.66, 0, 0.5f);
                     glVertex2fv(glm::value_ptr(eprime->origin));
                     glColor4f(0, 1, 0, 0.5f);
                     glVertex2fv(glm::value_ptr(eprime->next->origin));
                     glEnd();*/

                  it->second = e;

                  status.emplace(eprime->prev, eprime);
               } else {
                  status.emplace(e->prev, e);
               }
               break;
            }
         }
      }
   }
   
   return tris;
}

#endif
