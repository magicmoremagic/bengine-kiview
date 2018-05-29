#pragma once
#ifndef KIVIEW_APP_HPP_
#define KIVIEW_APP_HPP_

#include <be/core/be.hpp>
#include <be/core/lifecycle.hpp>
#include <be/core/extents.hpp>
#include <be/platform/lifecycle.hpp>
#include <glm/vec2.hpp>
#include <functional>
#include <random>

///////////////////////////////////////////////////////////////////////////////
class KiViewApp final {
public:
   KiViewApp(int argc, char** argv);
   int operator()();

private:
   void run_();
   void autoscale_();

   be::CoreInitLifecycle init_;
   be::CoreLifecycle core_;
   be::platform::PlatformLifecycle platform_;

   be::I8 status_ = 0;

   be::S filename_;
   be::rect board_bounds_;

   glm::ivec2 viewport_ = glm::ivec2(640, 480);

   glm::vec2 center_;
   be::F32 scale_ = 1;
   bool enable_autoscale_ = true;
   bool enable_autocenter_ = true;

   glm::vec2 relative_cursor_;
   glm::vec2 cursor_;
   bool dragging_ = false;
};

#endif
