#pragma once
#ifndef KIVIEW_LAYER_CONFIG_HPP_
#define KIVIEW_LAYER_CONFIG_HPP_

#include "pcb_helper.hpp"
#include <set>

struct StandardConfig {
   face_type face;
   layer_type layer;

   std::pair<bool, bool> operator()(const Node& node, const std::vector<const Node*>& stack) {
      return std::make_pair(check_layer(node, face, layer), true);
   }
};

struct CopperConfig {
   face_type face;
   bool skip_zones;
   std::set<be::U32>* skip_nets;
   std::set<be::U32>* include_nets;

   std::pair<bool, bool> operator()(const Node& node, const std::vector<const Node*>& stack) {
      using namespace std::string_view_literals;

      node_type type = get_node_type(node);
      if (skip_zones && type == node_type::n_zone) {
         return std::make_pair(false, false);
      }

      if (skip_nets || include_nets) {
         auto it = find(node, "net"sv);
         if (it != node.end()) {
            auto& child = *it;
            if (child.size() >= 2) {
               be::U32 net = (be::U32)child[1].value();
               if (skip_nets && skip_nets->count(net) > 0) {
                  return std::make_pair(false, false);
               }
               if (include_nets && include_nets->count(net) <= 0) {
                  return std::make_pair(false, false);
               }
            }
         } else if (include_nets) {
            return std::make_pair(false, true);
         }
      }

      return std::make_pair(check_layer(node, face, layer_type::l_copper), true);
   }
};

struct ModuleConfig {
   face_type face;
   bool include_court;
   std::set<const Node*>* include_nodes;

   std::pair<bool, bool> operator()(const Node& node, const std::vector<const Node*>& stack) {
      using namespace std::string_view_literals;

      if (check_layer(node, face, layer_type::l_copper) && get_node_type(node) == node_type::n_pad ||
          include_court && check_layer(node, face, layer_type::l_court)) {

         if (stack.size() < 2) {
            return std::make_pair(false, true);
         }

         const Node* module_node = nullptr;
         for (auto it = stack.rbegin() + 1, end = stack.rend(); it != end; ++it) {
            auto n = *it;
            if (get_node_type(*n) == node_type::n_module) {
               module_node = n;
            }
         }

         if (!module_node) {
            return std::make_pair(false, true);
         }

         if (include_nodes) {
            if (include_nodes->count(module_node) <= 0) {
               return std::make_pair(false, false);
            }
         }

         return std::make_pair(true, true);
      }

      return std::make_pair(false, true);
   }
};


struct HoleConfig {
   std::pair<bool, bool> operator()(const Node& node, const std::vector<const Node*>& stack) {
      return std::make_pair(get_node_type(node) == node_type::n_drill, true);
   }
};

#endif
