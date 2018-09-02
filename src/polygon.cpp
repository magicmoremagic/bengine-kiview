#include "polygon.hpp"
#include <be/core/be.hpp>
#include <glm/geometric.hpp>
#include <map>
#include <algorithm>

namespace {

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
be::F32 vertex_cos(glm::vec2 prev, glm::vec2 self, glm::vec2 next) {
   glm::vec2 ps = self - prev;
   glm::vec2 psn = glm::vec2(-ps.y, ps.x);
   return glm::dot(psn, next - self);
}

///////////////////////////////////////////////////////////////////////////////
be::F32 vertex_cos(edge* e) {
   return vertex_cos(e->prev->origin, e->origin, e->next->origin);
}

///////////////////////////////////////////////////////////////////////////////
bool is_reflex(glm::vec2 prev, glm::vec2 self, glm::vec2 next) {
   return vertex_cos(prev, self, next) < 0;
}

///////////////////////////////////////////////////////////////////////////////
bool is_reflex(edge* e) {
   return vertex_cos(e->prev->origin, e->origin, e->next->origin) < 0;
}

///////////////////////////////////////////////////////////////////////////////
bool is_convex(glm::vec2 prev, glm::vec2 self, glm::vec2 next) {
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

      return a->origin.y < b->origin.y;
   }
};

///////////////////////////////////////////////////////////////////////////////
be::F32 edge_y_at_sweep_x(edge* e, be::F32 x) {
   glm::vec2 o = e->origin;
   glm::vec2 delta = e->next->origin - o;
   if (delta.x == 0) {
      return e->next->origin.y;
   } else {
      return o.y + delta.y * (x - o.x) / delta.x;
   }
}

///////////////////////////////////////////////////////////////////////////////
struct StatusComp {
   bool operator()(edge* a, edge* b) const {
      glm::vec2 ao = a->origin;
      glm::vec2 bo = b->origin;

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
vertex_type get_vertex_type(const edge* e) {
   glm::vec2 p = e->prev->origin;
   glm::vec2 o = e->origin;
   glm::vec2 n = e->next->origin;

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
enum class chain {
   low,
   high
};

///////////////////////////////////////////////////////////////////////////////
struct monotone_vertex {
   glm::vec2 v;
   chain c;
};

///////////////////////////////////////////////////////////////////////////////
void triangulate_monotone_polygon(edge* start, std::vector<monotone_vertex>& stack, std::vector<triangle>& out) {
   stack.clear();
   stack.push_back(monotone_vertex { start->origin, chain::low }); // chain designation doesn't matter here so we just pick low arbitrarily

   edge* low = start->prev;
   edge* high = start->next;

   if (high == low) {
      // "polygon" is actually a degenerate triangle
      return;
   } else if (high->origin.x > low->origin.x) {
      stack.push_back(monotone_vertex { high->origin, chain::high });
      high = high->next;
   } else {
      stack.push_back(monotone_vertex { low->origin, chain::low });
      low = low->prev;
   }

   while (high != low) {
      if (high->origin.x > low->origin.x) {
         if (stack.back().c == chain::low) {
            // different chains
            auto it = stack.begin();
            auto next = it + 1;
            while (next != stack.end()) {
               out.push_back(triangle { { next->v, it->v, high->origin } });
               ++it;
               ++next;
            }
            monotone_vertex last = stack.back();
            stack.clear();
            stack.push_back(last);
            stack.push_back(monotone_vertex { high->origin, chain::high });
            high = high->next;
         } else {
            // same chain (high)
            while (stack.size() > 1) {
               auto it = stack.rbegin();
               monotone_vertex last = *it++;
               monotone_vertex prev = *it;

               if (is_convex(prev.v, last.v, high->origin)) {
                  out.push_back(triangle { { prev.v, last.v, high->origin } });
                  stack.pop_back();
               } else {
                  break;
               }
            }
            stack.push_back(monotone_vertex { high->origin, chain::high });
            high = high->next;
         }
      } else {
         if (stack.back().c == chain::high) {
            // different chains
            auto it = stack.begin();
            auto next = it + 1;
            while (next != stack.end()) {
               out.push_back(triangle { { it->v, next->v, low->origin } });
               ++it;
               ++next;
            }
            monotone_vertex last = stack.back();
            stack.clear();
            stack.push_back(last);
            stack.push_back(monotone_vertex { low->origin, chain::low });
            low = low->prev;
         } else {
            // same chain (low)
            while (stack.size() > 1) {
               auto it = stack.rbegin();
               monotone_vertex last = *it++;
               monotone_vertex prev = *it;

               if (is_convex(low->origin, last.v, prev.v)) {
                  out.push_back(triangle { { low->origin, last.v, prev.v } });
                  stack.pop_back();
               } else {
                  break;
               }
            }
            stack.push_back(monotone_vertex { low->origin, chain::low });
            low = low->prev;
         }
      }
   }

   // the end point is on both the high and low chains, but we need to get the winding order right
   if (stack.back().c == chain::high) {
      auto it = stack.begin();
      auto next = it + 1;
      while (next != stack.end()) {
         out.push_back(triangle { { it->v, next->v, low->origin } });
         ++it;
         ++next;
      }
   } else {
      auto it = stack.begin();
      auto next = it + 1;
      while (next != stack.end()) {
         out.push_back(triangle { { next->v, it->v, high->origin } });
         ++it;
         ++next;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
std::pair<edge*, edge*> insert_diagonal(edge* a, edge* b, std::deque<edge>& owner) {
   owner.push_back(edge { a->origin, a->prev, b });
   edge* aprime = &owner.back();

   owner.push_back(edge { b->origin, b->prev, a });
   edge* bprime = &owner.back();

   aprime->prev->next = aprime;
   aprime->next->prev = aprime;

   bprime->prev->next = bprime;
   bprime->next->prev = bprime;

   return std::make_pair(aprime, bprime);
}

} // ::()

///////////////////////////////////////////////////////////////////////////////
void make_dcel(const std::vector<glm::vec2>& verts, std::deque<edge>& out) {
   if (verts.empty()) {
      return;
   }

   auto old_size = out.size();

   glm::vec2 pv = verts.back();
   for (auto p : verts) {
      if (p != pv) {
         out.push_back(edge { p });
         pv = p;
      }
   }

   make_dcel(out.begin() + old_size, out.end());
}

///////////////////////////////////////////////////////////////////////////////
void make_dcel(std::deque<edge>::iterator begin, std::deque<edge>::iterator end) {
   for (auto it = begin; it != end; ++it) {
      edge& e = *it;
      if (it == begin) {
         auto last = end;
         --last;
         e.prev = &(*last);
      } else {
         auto prev = it;
         --prev;
         e.prev = &(*prev);
      }

      auto next = it;
      ++next;
      if (next == end) {
         next = begin;
      }
      e.next = &(*next);
   }
}

///////////////////////////////////////////////////////////////////////////////
void triangulate_polygon(std::deque<edge>& edges, std::vector<triangle>& out) {
   std::vector<monotone_vertex> stack;
   std::vector<edge*> events(edges.size());
   std::transform(edges.begin(), edges.end(), events.begin(), [](edge& e) { return &e; });
   std::sort(events.begin(), events.end(), VertexComp());

   struct helper {
      edge* split = nullptr;
      edge* merge = nullptr;
   };

   std::map<edge*, helper, StatusComp> status;

   for (auto eit = events.begin(), eend = events.end(); eit != eend; ++eit) {
      edge* e = *eit;

      glm::vec2 o = e->origin;

      if (e->next == nullptr || e->prev == nullptr) {
         continue;
      }

      auto enext = eit + 1;
      if (enext != eend) {
         // check for twin edges and remove/ignore them
         edge* en = *enext;
         if (en->next != nullptr && en->prev != nullptr) {
            if (e->origin == en->origin) {
               if (e->prev->origin == en->next->origin) {
                  edge* etp = e->prev;
                  edge* etn = en->next;

                  en->prev->next = e;
                  e->prev = en->prev;
                  en->prev = nullptr;
                  etp->next = nullptr;

                  etp->prev->next = etn;
                  etn->prev = etp->prev;
                  etp->prev = nullptr;
                  en->next = nullptr;
               } else if (en->prev->origin == e->next->origin) {
                  edge* etp = en->prev;
                  edge* etn = e->next;

                  e->prev->next = en;
                  en->prev = e->prev;
                  e->prev = nullptr;
                  etp->next = nullptr;

                  etp->prev->next = etn;
                  etn->prev = etp->prev;
                  etp->prev = nullptr;
                  e->next = nullptr;
               }

               if (e->next == nullptr || e->prev == nullptr) {
                  continue;
               }
            }
         }
      }

      auto sit = status.upper_bound(e);
      if (sit != status.begin()) {
         --sit;
         helper& h = sit->second;
         vertex_type type = get_vertex_type(e);

         if (h.merge) {
            // existing helper with a merge that needs to be resolved asap
            switch (type) {
               case vertex_type::start:
                  status.emplace(e, helper { e });
                  break;

               case vertex_type::end:
               {
                  auto [eprime, mprime] = insert_diagonal(e, h.merge, edges);
                  status.erase(sit);
                  triangulate_monotone_polygon(e, stack, out);
                  triangulate_monotone_polygon(eprime, stack, out);
                  break;
               }
               case vertex_type::split:
               {
                  auto [eprime, mprime] = insert_diagonal(e, h.merge, edges);
                  h.split = eprime;
                  h.merge = nullptr;
                  status.emplace(e, helper { e });
                  break;
               }
               case vertex_type::merge:
               {
                  auto [eprime, mprime] = insert_diagonal(e, h.merge, edges);
                  triangulate_monotone_polygon(eprime, stack, out);
                  sit = status.erase(sit);
                  if (sit != status.begin()) {
                     --sit;
                     if (sit->second.merge) {
                        auto [eprime2, mprime2] = insert_diagonal(e, sit->second.merge, edges);
                        triangulate_monotone_polygon(e, stack, out);
                        sit->second.split = sit->second.merge = eprime2;
                     } else {
                        sit->second.split = sit->second.merge = e;
                     }
                  }
                  break;
               }
               case vertex_type::low:
               {
                  auto [eprime, mprime] = insert_diagonal(e, h.merge, edges);
                  sit = status.erase(sit);
                  status.emplace_hint(sit, e, helper { e });
                  triangulate_monotone_polygon(eprime, stack, out);
                  break;
               }
               case vertex_type::high:
               {
                  auto [eprime, mprime] = insert_diagonal(e, h.merge, edges);
                  h.split = eprime;
                  h.merge = nullptr;
                  triangulate_monotone_polygon(e, stack, out);
                  break;
               }
            }
         } else {
            // existing segment, but no merge to resolve
            switch (type) {
               case vertex_type::start:
                  status.emplace(e, helper { e });
                  break;

               case vertex_type::end:
                  status.erase(sit);
                  triangulate_monotone_polygon(e, stack, out);
                  break;

               case vertex_type::low:
                  sit = status.erase(sit);
                  status.emplace_hint(sit, e, helper { e });
                  break;

               case vertex_type::high:
                  h.split = e;
                  break;

               case vertex_type::merge:
                  sit = status.erase(sit);
                  if (sit != status.begin()) {
                     --sit;
                     if (sit->second.merge) {
                        auto [eprime, mprime] = insert_diagonal(e, sit->second.merge, edges);
                        triangulate_monotone_polygon(e, stack, out);
                        sit->second.split = sit->second.merge = eprime;
                     } else {
                        sit->second.split = sit->second.merge = e;
                     }
                  }
                  break;

               case vertex_type::split:
               {
                  auto [eprime, sprime] = insert_diagonal(e, h.split, edges);
                  h.split = eprime;
                  status.emplace(e, helper { e });
                  break;
               }
            }
         }
      } else {
         // no segment yet; should be start vertex.
         status.emplace(e, helper { e });
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
std::vector<triangle> triangulate_polygon(const std::vector<glm::vec2>& verts) {
   std::vector<triangle> out;
   std::deque<edge> edges;
   make_dcel(verts, edges);
   triangulate_polygon(edges, out);
   return out;
}
