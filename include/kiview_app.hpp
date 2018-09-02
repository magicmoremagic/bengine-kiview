#pragma once
#ifndef KIVIEW_APP_HPP_
#define KIVIEW_APP_HPP_

#include "node.hpp"

#include <be/core/lifecycle.hpp>
#include <be/core/extents.hpp>
#include <be/util/string_interner.hpp>
#include <be/platform/lifecycle.hpp>
#include <be/platform/glfw_window.hpp>
#include <glm/vec2.hpp>
#include <functional>
#include <random>
#include <set>

///////////////////////////////////////////////////////////////////////////////
class KiViewApp final {
public:
   KiViewApp(int argc, char** argv);
   int operator()();

private:
   void run_();
   void load_(be::SV filename);
   void autoscale_();
   void select_at_(glm::vec2 pos);
   void select_all_like_(const Node& mod);
   void process_command_(be::SV cmd);
   void set_segment_density_(be::SV params, void(*fp)(be::U32), be::SV label);
   void render_();

   be::CoreInitLifecycle init_;
   be::CoreLifecycle core_;
   be::platform::PlatformLifecycle platform_;

   be::I8 status_ = 0;

   be::S filename_;

   be::util::StringInterner si_;
   Node root_;
   be::rect board_bounds_;
   be::U32 ground_net_ = 0;

   GLFWwindow* wnd_;
   glm::ivec2 viewport_ = glm::ivec2(640, 480);

   glm::vec2 center_;
   be::F32 scale_ = 1;
   bool enable_autoscale_ = true;
   bool enable_autocenter_ = true;

   glm::vec2 relative_cursor_;
   glm::vec2 cursor_;
   bool dragging_ = false;

   be::S info_;
   bool input_enabled_ = false;

   bool select_only_modules_ = false;
   bool select_only_nets_ = false;

   bool flipped_ = false;
   bool wireframe_ = false;
   bool see_thru_ = false;
   bool skip_copper_ = false;
   bool skip_silk_ = false;
   bool skip_zones_ = false;
   std::set<be::U32> skip_nets_;
   std::set<be::U32> highlight_nets_;
   std::set<const Node*> highlight_modules_;
};

#endif
