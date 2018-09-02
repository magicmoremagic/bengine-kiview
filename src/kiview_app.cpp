#include "kiview_app.hpp"
#include "node.hpp"
#include "render_layer.hpp"
#include "layer_config.hpp"

#include <be/core/logging.hpp>
#include <be/core/version.hpp>
#include <be/core/stack_trace.hpp>
#include <be/core/alg.hpp>
#include <be/util/keyword_parser.hpp>
#include <be/util/parse_numeric_string.hpp>
#include <be/util/get_file_contents.hpp>
#include <be/platform/glfw_window.hpp>
#include <be/gfx/version.hpp>
#include <be/cli/cli.hpp>
#include <be/gfx/bgl.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>
#include <stb/stb_easy_font.h>
#include <sstream>
#include <iostream>
#include <string>

using namespace std::string_view_literals;
using namespace be;
using namespace be::gfx;
using namespace be::gfx::gl;

///////////////////////////////////////////////////////////////////////////////
KiViewApp::KiViewApp(int argc, char** argv) {
   default_log().verbosity_mask(v::info_or_worse);
   try {
      using namespace cli;
      using namespace color;
      using namespace ct;
      Processor proc;

      bool show_version = false;
      bool show_help = false;
      bool verbose = false;
      S help_query;

      proc
         (prologue(Table() << header << "KiView").query())
         (synopsis(Cell() << fg_dark_gray << "[ " << fg_cyan << "OPTIONS" << fg_dark_gray << " ] [ " << fg_cyan << "filename" << fg_dark_gray << " ]"))
         (any([this](const S& value) {
            filename_ = value;
            return true;
         }))
         (end_of_options())
         (verbosity_param({ "v" }, { "verbosity" }, "LEVEL", default_log().verbosity_mask()))
         (flag({ "V" }, { "version" }, show_version).desc("Prints version information to standard output."))
         (param({ "?" }, { "help" }, "OPTION", [&](const S& value) {
               show_help = true;
               help_query = value;
            }).default_value(S())
               .allow_options_as_values(true)
               .desc(Cell() << "Outputs this help message.  For more verbose help, use " << fg_yellow << "--help")
               .extra(Cell() << nl << "If " << fg_cyan << "OPTION" << reset
                             << " is provided, the options list will be filtered to show only options that contain that string."))
         (flag({ }, { "help" }, verbose).ignore_values(true))
         (exit_code(0, "There were no errors."))
         (exit_code(1, "An unknown error occurred."))
         (exit_code(2, "There was a problem parsing the command line arguments."))
         ;

      proc.process(argc, argv);

      if (show_version) {
         proc
            (prologue(BE_CORE_VERSION_STRING).query())
            (prologue(BE_GFX_VERSION_STRING).query())
            (license(BE_LICENSE).query())
            (license(BE_COPYRIGHT).query())
            ;
      }

      if (show_help) {
         proc.describe(std::cout, verbose, help_query);
      } else if (show_version) {
         proc.describe(std::cout, verbose, ids::cli_describe_section_prologue);
         proc.describe(std::cout, verbose, ids::cli_describe_section_license);
      }
   } catch (const cli::OptionError& e) {
      status_ = 2;
      be_error() << S(e.what())
         & attr(ids::log_attr_index) << e.raw_position()
         & attr(ids::log_attr_argument) << S(e.argument())
         & attr(ids::log_attr_option) << S(e.option())
         | default_log();
   } catch (const cli::ArgumentError& e) {
      status_ = 2;
      be_error() << S(e.what())
         & attr(ids::log_attr_index) << e.raw_position()
         & attr(ids::log_attr_argument) << S(e.argument())
         | default_log();
   } catch (const FatalTrace& e) {
      status_ = 2;
      be_error() << "Fatal error while parsing command line!"
         & attr(ids::log_attr_message) << S(e.what())
         & attr(ids::log_attr_trace) << StackTrace(e.trace())
         | default_log();
   } catch (const std::exception& e) {
      status_ = 2;
      be_error() << "Unexpected exception parsing command line!"
         & attr(ids::log_attr_message) << S(e.what())
         | default_log();
   }
}

///////////////////////////////////////////////////////////////////////////////
int KiViewApp::operator()() {
   if (status_ != 0) {
      return status_;
   }

   try {
      run_();
   } catch (const FatalTrace& e) {
      status_ = std::max(status_, (I8)1);
      be_error() << "Unexpected fatal error!"
         & attr(ids::log_attr_message) << S(e.what())
         & attr(ids::log_attr_trace) << StackTrace(e.trace())
         | default_log();
   } catch (const fs::filesystem_error& e) {
      status_ = std::max(status_, (I8)1);
      be_error() << "Unexpected error!"
         & attr(ids::log_attr_message) << S(e.what())
         & attr(ids::log_attr_category) << e.code().category().name()
         & attr(ids::log_attr_error_code) << e.code().value()
         & attr(ids::log_attr_error) << e.code().message()
         & attr(ids::log_attr_path) << e.path1().generic_string()
         & attr(ids::log_attr_path) << e.path2().generic_string()
         | default_log();
   } catch (const std::system_error& e) {
      status_ = std::max(status_, (I8)1);
      be_error() << "Unexpected error!"
         & attr(ids::log_attr_message) << S(e.what())
         & attr(ids::log_attr_category) << e.code().category().name()
         & attr(ids::log_attr_error_code) << e.code().value()
         & attr(ids::log_attr_error) << e.code().message()
         | default_log();
   } catch (const std::exception& e) {
      status_ = std::max(status_, (I8)1);
      be_error() << "Unexpected exception!"
         & attr(ids::log_attr_message) << S(e.what())
         | default_log();
   }

   return status_;
}

namespace {

///////////////////////////////////////////////////////////////////////////////
void GLAPIENTRY check_errors(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
   be_warn() << "OpenGL Error"
      & attr("Source") << source
      & attr("Type") << type
      & attr("ID") << id
      & attr("Severity") << severity
      & attr("Message") << message
      & attr("Trace") << get_stack_trace()
      | default_log();
}

///////////////////////////////////////////////////////////////////////////////
rect get_area(const Node& pcb) {
   Node::const_iterator it = find(pcb, "general");
   if (it != pcb.end()) {
      Node::const_iterator area_it = find(*it, "area");
      if (area_it != it->end()) {
         const Node& area = *area_it;
         if (area.size() >= 5) {
            rect r;
            r.offset.x = (F32)area[1].value();
            r.offset.y = (F32)area[2].value();
            vec2 b((F32)area[3].value(), (F32)area[4].value());
            return r.union_bounds(rect { b, vec2() });
         }
      }
   }
   return rect();
}

///////////////////////////////////////////////////////////////////////////////
void draw_layer(const Node& root, const RenderNodePredicate& func, glm::vec4 color, bool wireframe) {
   std::vector<triangle> tris = render_layer(root, func);
   glColor4fv(glm::value_ptr(color));

   if (wireframe) {
      for (auto& tri : tris) {
         glBegin(GL_LINE_LOOP);
         glVertex2fv(glm::value_ptr(tri.v[0]));
         glVertex2fv(glm::value_ptr(tri.v[1]));
         glVertex2fv(glm::value_ptr(tri.v[2]));
         glEnd();
      }
   } else {
      glBegin(GL_TRIANGLES);
      for (auto& tri : tris) {
         glVertex2fv(glm::value_ptr(tri.v[0]));
         glVertex2fv(glm::value_ptr(tri.v[1]));
         glVertex2fv(glm::value_ptr(tri.v[2]));
      }
      glEnd();
   }
}

///////////////////////////////////////////////////////////////////////////////
enum class Alignment {
   left,
   center,
   right
};

///////////////////////////////////////////////////////////////////////////////
void draw_text(const be::S& text, glm::vec2 pos, Alignment alignment, glm::vec4 color, bool wireframe) {
   struct vertex {
      glm::vec2 pos;
      be::F32 z;
      RGBA color;
   };

   static Buf<vertex> vbuf = make_buf<vertex>(1000);

   if (alignment != Alignment::left) {
      int w = stb_easy_font_width(const_cast<char*>(text.c_str()));

      if (alignment == Alignment::right) {
         pos.x -= w;
      } else {
         pos.x -= w * 0.5f;
      }
   }

   int quads = stb_easy_font_print(pos.x, pos.y,
      const_cast<char*>(text.c_str()), nullptr,
      vbuf.get(), (int)(vbuf.size() * sizeof(vertex)));

   glColor4fv(glm::value_ptr(color));

   if (wireframe) {
      for (int q = 0; q < quads; ++q) {
         int offset = q * 4;
         glBegin(GL_LINE_LOOP);
         glVertex2fv(glm::value_ptr(vbuf[offset + 0].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 1].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 2].pos));
         glEnd();
         glBegin(GL_LINE_LOOP);
         glVertex2fv(glm::value_ptr(vbuf[offset + 2].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 1].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 3].pos));
         glEnd();
      }
   } else {
      glBegin(GL_TRIANGLES);
      for (int q = 0; q < quads; ++q) {
         int offset = q * 4;
         glVertex2fv(glm::value_ptr(vbuf[offset + 0].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 1].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 2].pos));

         glVertex2fv(glm::value_ptr(vbuf[offset + 0].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 2].pos));
         glVertex2fv(glm::value_ptr(vbuf[offset + 3].pos));
      }
      glEnd();
   }
}

///////////////////////////////////////////////////////////////////////////////
util::KeywordParser<bool> bool_parser() {
   static util::KeywordParser<bool> parser = std::move(
      util::KeywordParser<bool>(false)
         (true, "on", "true", "1", "enabled")
      );
   return parser;
}

///////////////////////////////////////////////////////////////////////////////
const Node* find_closest_module(const Node& root, glm::vec2 target, be::F32& max_distance, face_type side) {
   const Node* closest = nullptr;

   auto& parser = node_type_parser();
   for (const Node& child : root) {
      if (!child.empty()) {
         node_type child_type = parser.parse(child[0].text());
         if (child_type != node_type::ignored) {
            
            switch (child_type) {
               case node_type::n_kicad_pcb:
                  return find_closest_module(child, target, max_distance, side);

               case node_type::n_module:
                  if (check_layer(child, side, layer_type::any)) {
                     auto it = find(child, "at"sv);
                     if (it != child.end()) {
                        const Node& at = *it;
                        if (at.size() >= 3) {
                           glm::vec2 at_pos = glm::vec2((be::F32)at[1].value(), (be::F32)at[2].value());
                           if (glm::distance2(at_pos, target) < max_distance * max_distance) {
                              closest = &child;
                              max_distance = glm::distance(at_pos, target);
                           }
                        }
                     }
                  }
                  break;
            }
         }
      }
   }

   return closest;
}

///////////////////////////////////////////////////////////////////////////////
const Node* find_closest_segment_or_via(const Node& root, glm::vec2 target, be::F32& max_distance, face_type side, const std::set<be::U32>& skip_nets) {
   const Node* closest = nullptr;

   auto& parser = node_type_parser();
   for (const Node& child : root) {
      if (!child.empty()) {
         node_type child_type = parser.parse(child[0].text());
         if (child_type != node_type::ignored) {
            
            switch (child_type) {
               case node_type::n_kicad_pcb:
                  return find_closest_segment_or_via(child, target, max_distance, side, skip_nets);

               case node_type::n_segment:
                  if (check_layer(child, side, layer_type::any)) {
                     auto nit = find(child, "net"sv);
                     auto sit = find(child, "start"sv);
                     auto eit = find(child, "end"sv);
                     if (nit != child.end() && sit != child.end() && eit != child.end()) {
                        const Node& net = *nit;
                        const Node& start = *sit;
                        const Node& end = *eit;
                        if (net.size() >= 2 && start.size() >= 3 && end.size() >= 3) {
                           if (skip_nets.count((be::U32)net[1].value()) > 0) {
                              break;
                           }

                           glm::vec2 start_pos = glm::vec2((be::F32)start[1].value(), (be::F32)start[2].value());
                           glm::vec2 end_pos = glm::vec2((be::F32)end[1].value(), (be::F32)end[2].value());
                           glm::vec2 delta = end_pos - start_pos;
                           if (glm::dot(delta, target - end_pos) >= 0) {
                              if (glm::distance2(end_pos, target) < max_distance * max_distance) {
                                 closest = &child;
                                 max_distance = glm::distance(end_pos, target);
                              }
                           } else if (glm::dot(-delta, target - start_pos) >= 0) {
                              if (glm::distance2(start_pos, target) < max_distance * max_distance) {
                                 closest = &child;
                                 max_distance = glm::distance(start_pos, target);
                              }
                           } else {
                              glm::vec2 normal = glm::normalize(glm::vec2(-delta.y, delta.x));
                              F32 distance = std::abs(glm::dot(normal, target - start_pos));
                              if (distance < max_distance) {
                                 closest = &child;
                                 max_distance = distance;
                              }
                           }
                        }
                     }
                  }
                  break;

               case node_type::n_via:
                  if (check_layer(child, side, layer_type::any)) {
                     auto nit = find(child, "net"sv);
                     auto it = find(child, "at"sv);
                     if (nit != child.end() && it != child.end()) {
                        const Node& net = *nit;
                        const Node& at = *it;
                        if (net.size() >= 2 && at.size() >= 3) {
                           if (skip_nets.count((be::U32)net[1].value()) > 0) {
                              break;
                           }

                           glm::vec2 at_pos = glm::vec2((be::F32)at[1].value(), (be::F32)at[2].value());
                           if (glm::distance2(at_pos, target) < max_distance * max_distance) {
                              closest = &child;
                              max_distance = glm::distance(at_pos, target);
                           }
                        }
                     }
                  }
                  break;
            }
         }
      }
   }

   return closest;
}

} // ::()

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::run_() {
   si_.provisioning_policy([](std::size_t s) { return min(s * 2, 0x1000000ull) + 0x10000; });

   stb_easy_font_spacing(-1);

   glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
   glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
   glfwWindowHint(GLFW_SAMPLES, 4);
   wnd_ = glfwCreateWindow(viewport_.x, viewport_.y, "KiView", nullptr, nullptr);
   glfwMakeContextCurrent(wnd_);
   glfwSwapInterval(1);
   glfwSetWindowUserPointer(wnd_, this);

   gl::init_context();

   if (GL_KHR_debug) {
      //#bgl checked(GL_KHR_debug)
      glEnable(GL_DEBUG_OUTPUT);
      glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
      glDebugMessageCallback(check_errors, nullptr);
      //#bgl unchecked
   }

   glViewport(0, 0, viewport_.x, viewport_.y);
   glClearColor(0, 0, 0, 0);
   glEnable(GL_BLEND);
   glBlendEquation(GL_FUNC_ADD);
   glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

   load_(filename_);
   autoscale_();

   glfwSetWindowSizeCallback(wnd_, [](GLFWwindow* wnd, int w, int h) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      ivec2 new_size { w, h };

      glViewport(0, 0, new_size.x, new_size.y);

      if (new_size != app.viewport_ && new_size.x * new_size.y > 0) {
         app.viewport_ = new_size;
         app.autoscale_();
         glfwPostEmptyEvent();
      }
   });

   glfwSetScrollCallback(wnd_, [](GLFWwindow* wnd, double x, double y) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      app.scale_ *= (F32)std::pow(1.2, y);
      app.enable_autoscale_ = false;
   });

   glfwSetCursorPosCallback(wnd_, [](GLFWwindow* wnd, double x, double y) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      vec2 offset = vec2(dvec2(x, y)) - vec2(app.viewport_) / 2.f;
      vec2 scaled_offset = offset / app.scale_;

      if (app.dragging_) {
         auto delta = scaled_offset - app.relative_cursor_;
         if (app.flipped_) {
            delta.x *= -1;
         }
         app.center_ -= delta;
         app.enable_autocenter_ = false;
      }

      app.relative_cursor_ = scaled_offset;

      if (app.flipped_) {
         app.cursor_ = app.center_ + vec2(-scaled_offset.x, scaled_offset.y);
      } else {
         app.cursor_ = app.center_ + scaled_offset;
      }
   });

   glfwSetMouseButtonCallback(wnd_, [](GLFWwindow* wnd, int btn, int action, int mods) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      if (btn == GLFW_MOUSE_BUTTON_1) {
         if (action == GLFW_RELEASE) {
            app.select_at_(app.cursor_);
         }
      } else if (btn == GLFW_MOUSE_BUTTON_3) {
         if (action == GLFW_RELEASE) {
            app.dragging_ = false;
         } else {
            app.dragging_ = true;
         }
      } else if (btn == GLFW_MOUSE_BUTTON_2) {
         if (action == GLFW_RELEASE) {
            app.center_ = app.cursor_;
         }
      }
   });

   glfwSetKeyCallback(wnd_, [](GLFWwindow* wnd, int key, int scancode, int action, int mods) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      if (action == GLFW_RELEASE) {
         if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (app.input_enabled_) {
               app.input_enabled_ = false;
               app.process_command_(app.info_);
            } else {
               app.process_command_("\n"sv);
            }
         } else if (key == GLFW_KEY_BACKSPACE) {
            if (app.input_enabled_ && !app.info_.empty()) {
               app.info_.pop_back();
            }
         }
      }
   });

   glfwSetCharCallback(wnd_, [](GLFWwindow* wnd, unsigned int character) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));

      if (app.input_enabled_) {
         if (character == '\r' || character == '\n') {
            app.input_enabled_ = false;
            app.process_command_(app.info_);
         } else {
            app.info_.append(1, (char)character);
         }
      } else {
         S str;
         str.append(1, (char)character);
         app.process_command_(str);
      }
   });

   while (!glfwWindowShouldClose(wnd_)) {
      glfwWaitEvents();
      render_();
      glfwSwapBuffers(wnd_);
   }

   glfwDestroyWindow(wnd_);
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::load_(be::SV filename) {
   S file = be::util::get_text_file_contents_string(filename_);
   root_ = parse(file, si_);
   
   Node::const_iterator iter = find(root_, "kicad_pcb"sv);
   
   S window_title = filename_;

   if (iter != root_.end()) {
      const Node& pcb = *iter;
      Node::const_iterator title_block_it = find(pcb, "title_block"sv);
      if (title_block_it != pcb.end()) {
         Node::const_iterator title_it = find(*title_block_it, "title"sv);
         if (title_it != title_block_it->end()) {
            const Node& title = *title_it;
            if (title.size() >= 2) {
               window_title = title[1].text();
            }
         }
      }

      for (auto& child : pcb) {
         if (child.size() >= 3 && get_node_type(child) == node_type::n_net && child[2].text() == "GND"sv) {
            ground_net_ = (be::U32)child[1].value();
         }
      }

      board_bounds_ = get_area(pcb);
   }

   window_title = "KiView - " + window_title;

   glfwSetWindowTitle(wnd_, window_title.c_str());
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::autoscale_() {
   if (enable_autocenter_) {
      center_ = board_bounds_.center();
   }

   if (enable_autoscale_) {
      vec2 scale = vec2(viewport_ - ivec2(0, 66)) * 0.98f / board_bounds_.dim;
      scale_ = min(scale.x, scale.y);
   }
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::select_at_(glm::vec2 pos) {
   face_type fg = flipped_ ? face_type::f_back : face_type::f_front;
   face_type bg = flipped_ ? face_type::f_front : face_type::f_back;

   highlight_nets_.clear();
   highlight_modules_.clear();

   be::F32 distance = 254.f / scale_;
   const Node* selected = nullptr;

   if (!select_only_nets_) {
      selected = find_closest_module(root_, pos, distance, fg);

      if (see_thru_) {
         be::F32 bg_distance = selected ? distance / 2.f : distance;
         const Node* bg_selected = find_closest_module(root_, pos, bg_distance, bg);
         if (bg_selected) {
            selected = bg_selected;
            distance = bg_distance;
         }
      }
   }

   if (!select_only_modules_ && (select_only_nets_ || !skip_copper_)) {
      be::F32 cu_distance = selected ? distance / 2.f : distance;
      const Node* cu_selected = find_closest_segment_or_via(root_, pos, cu_distance, fg, skip_nets_);
      if (cu_selected) {
         selected = cu_selected;
         distance = cu_distance;
      }
      
      if (see_thru_) {
         be::F32 bg_distance = selected ? distance / 2.f : distance;
         const Node* bg_selected = find_closest_segment_or_via(root_, pos, bg_distance, bg, skip_nets_);
         if (bg_selected) {
            selected = bg_selected;
            distance = bg_distance;
         }
      }
   }

   select_only_modules_ = false;
   select_only_nets_ = false;
   input_enabled_ = false;
   info_ = "Nothing to select";
   if (selected) {
      if (get_node_type(*selected) == node_type::n_module) {
         highlight_modules_.insert(selected);
         info_ = "Selected Module";
      } else {
         auto it = find(*selected, "net"sv);
         if (it != selected->end()) {
            const Node& net = *it;
            if (net.size() >= 2) {
               be::U32 n = (be::U32)net[1].value();
               highlight_nets_.insert(n);
               info_ = "Selected Net";
            }
         }
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::select_all_like_(const Node& mod) {
   SV footprint = mod.size() >= 2 ? mod[1].text() : ""sv;
   char ref_type = 0;
   SV value_text = ""sv;
   F64 value = -1.f;

   for (auto& child : mod) {
      if (get_node_type(child) == node_type::n_fp_text) {
         if (child.size() >= 3) {
            if (child[1].text() == "reference"sv) {
               if (!child[2].text().empty()) {
                  ref_type = child[2].text()[0];
               }
            } else if (child[1].text() == "value"sv) {
               value_text = child[2].text();
               value = child[2].value();
            }
         }
      }
   }

   highlight_nets_.clear();
   highlight_modules_.clear();

   auto pcb = find(root_, "kicad_pcb"sv);
   if (pcb != root_.end()) {
      for (const Node& child : *pcb) {
         if (get_node_type(child) == node_type::n_module && child.size() >= 2 && child[1].text() == footprint) {
            bool found_value = false;
            bool found_ref = false;
            for (const Node& mod_child : child) {
               if (get_node_type(mod_child) == node_type::n_fp_text) {
                  if (mod_child.size() >= 3) {
                     if (mod_child[1].text() == "reference"sv) {
                        if (!mod_child[2].text().empty()) {
                           if (ref_type == mod_child[2].text()[0]) {
                              found_ref = true;
                           }
                        }
                     } else if (mod_child[1].text() == "value"sv) {
                        if (value_text == mod_child[2].text() && value == mod_child[2].value()) {
                           found_value = true;
                        }
                     }
                  }
               }
            }

            if (found_value && found_ref) {
               highlight_modules_.insert(&child);
            }
         }
      }
   }

   std::ostringstream oss;
   if (highlight_modules_.size() == 1) {
      oss << "Found 1 similar module";
   } else {
      oss << "Found " << highlight_modules_.size() << " similar modules";
   }
   info_ = oss.str();
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::process_command_(be::SV full_command) {
   if (full_command == " "sv) {
      enable_autoscale_ = true;
      enable_autocenter_ = true;
      autoscale_();
   }
   
   SV cmd, params;
   std::size_t i = full_command.find(' ', 0);
   if (i == SV::npos) {
      cmd = full_command;
   } else {
      cmd = full_command;
      cmd.remove_suffix(cmd.size() - i);
      params = full_command;
      params.remove_prefix(i + 1);
   }

   S cmd_lower(cmd);
   std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), be::to_lower<char>);

   if (cmd.empty()) {
      info_.clear();
      return;
   }

   if (cmd.size() == 1) {
      switch (cmd[0]) {
         case 'v':
            flipped_ = !flipped_;
            info_ = flipped_ ? "Back side" : "Front side";
            break;

         case 't':
            see_thru_ = !see_thru_;
            info_ = see_thru_ ? "Transparent substrate" : "Opaque Substrate";
            break;

         case 'c':
            skip_copper_ = !skip_copper_;
            info_ = skip_copper_ ? "Copper hidden" : "Copper shown";
            break;

         case 's':
            skip_silk_ = !skip_silk_;
            info_ = skip_silk_ ? "Silkscreen hidden" : "Silkscreen shown";
            break;

         case 'z':
            skip_zones_ = !skip_zones_;
            info_ = skip_zones_ ? "Zones hidden" : "Zones shown";
            break;

         case 'm':
            select_only_modules_ = true;
            select_only_nets_ = false;
            info_ = "Click to select module";
            break;

         case 'n':
            select_only_nets_ = true;
            select_only_modules_ = false;
            info_ = "Click to select net";
            break;

         case 'a':
            if (highlight_modules_.empty()) {
               info_ = "No modules selected";
            } else {
               select_all_like_(**highlight_modules_.begin());
            }
            break;

         case '/':
         case '\r':
         case '\n':
            info_.clear();
            input_enabled_ = true;
            break;

         case 'g':
            if (skip_nets_.count(ground_net_) > 0) {
               skip_nets_.erase(ground_net_);
               info_ = "Ground Copper Shown";
            } else {
               skip_nets_.insert(ground_net_);
               info_ = "Ground Copper Hidden";
            }
            break;

         default:
            info_ = "Unknown command: ";
            info_.append(cmd);
            break;
      }
   } else if (cmd_lower == "wireframe"sv) {
      wireframe_ = bool_parser().parse(params);
   } else if (cmd_lower == "pad_density"sv) {
      set_segment_density_(params, pad_segment_density, " edges/pad");
   } else if (cmd_lower == "endcap_density"sv) {
      set_segment_density_(params, endcap_segment_density, " edges/endcap");
   } else if (cmd_lower == "arc_density"sv) {
      set_segment_density_(params, arc_segment_density, " edges/circle");
   } else if (cmd_lower == "zone_endcap_density"sv) {
      set_segment_density_(params, zone_perimeter_endcap_segment_density, " edges/zone border endcap");
   } else if (cmd_lower == "hide"sv) {
      if (highlight_nets_.empty()) {
         info_ = "No selected nets to hide";
      } else {
         skip_nets_.insert(highlight_nets_.begin(), highlight_nets_.end());
         highlight_nets_.clear();
         info_ = "Selected nets hidden";
      }
   } else if (cmd_lower == "clear_hidden_nets") {
      skip_nets_.clear();
      info_ = "No hidden nets";
   } else {
      info_ = "Unknown command: ";
      info_.append(cmd_lower);
   }
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::set_segment_density_(be::SV params, void(*fp)(be::U32), be::SV label) {
   std::error_code ec;
   be::U32 segments = util::parse_bounded_numeric_string<be::U32>(params, 0, 360, ec);
   if (!ec) {
      fp(segments);
      std::ostringstream oss;
      oss << segments << label;
      info_ = oss.str();
   } else {
      info_ = "Failed to parse integer!";
   }
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::render_() {
   glClear(GL_COLOR_BUFFER_BIT);

   glm::vec3 scale = vec3(scale_);
   if (flipped_) {
      scale.x *= -1;
   }

   mat4 proj = glm::ortho(0.f, (F32)viewport_.x, (F32)viewport_.y, 0.f);
   mat4 view = glm::translate(
      glm::scale(
      glm::translate(
      mat4(),
      vec3(viewport_, 0.f) / 2.f),
      scale),
      vec3(-center_, 0.f));

   glMatrixMode(GL_PROJECTION);
   glLoadMatrixf(glm::value_ptr(proj));

   glMatrixMode(GL_MODELVIEW);
   glLoadMatrixf(glm::value_ptr(view));

   glm::vec3 h[2]  = { glm::vec3(0.0f), glm::vec3(0.1f) };
   glm::vec3 c[2]  = { glm::vec3(0.2f), glm::vec3(0.4f) };
   glm::vec3 p[2]  = { glm::vec3(0.2f), glm::vec3(0.4f) };
   glm::vec3 ch[2] = { glm::vec3(0.8f), glm::vec3(1.0f) };
   glm::vec3 ph[2] = { glm::vec3(0.8f), glm::vec3(1.0f) };

   if (highlight_modules_.empty()) {
      p[0] = glm::vec3(0.4f);
      p[1] = glm::vec3(0.8f);
   }

   glm::vec4 silk = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);
   glm::vec4 edge_cuts = glm::vec4(0.3f, 0.6f, 0.8f, 1.0f);

   glm::vec3 fv = flipped_ ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
   glm::vec3 bv = flipped_ ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

   glm::vec4 cb = glm::vec4(glm::mix(c[0], c[1], bv), 1.f);
   glm::vec4 pb = glm::vec4(glm::mix(p[0], p[1], bv), 1.f);
   glm::vec4 chb = glm::vec4(glm::mix(ch[0], ch[1], bv), 1.f);
   glm::vec4 phb = glm::vec4(glm::mix(ph[0], ph[1], bv), 1.f);

   glm::vec4 hf = glm::vec4(glm::mix(h[0], h[1], fv), 1.f);
   glm::vec4 cf = glm::vec4(glm::mix(c[0], c[1], fv), 1.f);
   glm::vec4 pf = glm::vec4(glm::mix(p[0], p[1], fv), 1.f);
   glm::vec4 chf = glm::vec4(glm::mix(ch[0], ch[1], fv), 1.f);
   glm::vec4 phf = glm::vec4(glm::mix(ph[0], ph[1], fv), 1.f);
   
   face_type foreground = flipped_ ? face_type::f_back : face_type::f_front;
   face_type background = flipped_ ? face_type::f_front: face_type::f_back;
   
   if (see_thru_) {
      if (!skip_copper_) {
         draw_layer(root_, CopperConfig { background, skip_zones_, &skip_nets_, nullptr }, cb, wireframe_);
      }
      draw_layer(root_, ModuleConfig { background, false, nullptr }, cb, wireframe_);
      draw_layer(root_, CopperConfig { background, false, false, &highlight_nets_ }, chb, wireframe_);
      draw_layer(root_, ModuleConfig { background, true, &highlight_modules_ }, phb, wireframe_);
   }

   if (!skip_copper_) {
      draw_layer(root_, CopperConfig { foreground, skip_zones_, &skip_nets_, nullptr }, cf, wireframe_);
   }
      
   draw_layer(root_, ModuleConfig { foreground, false, nullptr }, pf, wireframe_);
   draw_layer(root_, CopperConfig { foreground, false, false, &highlight_nets_ }, chf, wireframe_);
   draw_layer(root_, ModuleConfig { foreground, true, &highlight_modules_ }, phf, wireframe_);

   if (!skip_silk_) {
      draw_layer(root_, StandardConfig { foreground, layer_type::l_silk }, silk, wireframe_);
   }

   draw_layer(root_, HoleConfig(), hf, wireframe_);
   draw_layer(root_, StandardConfig { face_type::any, layer_type::l_cuts }, edge_cuts, wireframe_);


   view = glm::scale(mat4(), vec3(3.f));
   glLoadMatrixf(glm::value_ptr(view));

   glm::vec2 bounds = vec2(viewport_) / 3.f;
   be::F32 text_bg_height = 11.f;

   glBegin(GL_QUADS);
   glColor4fv(glm::value_ptr(glm::vec4(0, 0, 0, 0.6f)));
   glVertex2f(0, 0);
   glVertex2f(0, text_bg_height);
   glVertex2f(bounds.x, text_bg_height);
   glVertex2f(bounds.x, 0);

   glVertex2f(0, bounds.y - text_bg_height);
   glVertex2f(0, bounds.y);
   glVertex2f(bounds.x, bounds.y);
   glVertex2f(bounds.x, bounds.y - text_bg_height);
   glEnd();

   glm::vec4 text_color = vec4(0.66f, 0.7f, 0.75f, 1.f);
   glm::vec4 text_entry_color = vec4(0.9f, 0.8f, 0.7f, 1.f);

   if (input_enabled_) {
      S text = info_;
      text.append(1, '_');
      draw_text(text, glm::vec2(2), Alignment::left, text_entry_color, false);
   } else {
      draw_text(info_, glm::vec2(2), Alignment::left, text_color, false);
   }
   
   std::ostringstream oss;
   oss << cursor_.x << ", " << cursor_.y;
   draw_text(oss.str(), vec2(bounds.x - 2.f, 2.f), Alignment::right, text_color, false);

   // Bottom row: selection <ref>      <value>     <x>, <y>       <F/B> <T> <S> <C> <Z>
   if (highlight_modules_.size() == 1 && highlight_nets_.empty()) {
      const Node* mod = *highlight_modules_.begin();

      auto it = find(*mod, "at"sv);
      if (it != mod->end()) {
         auto& at = *it;
         if (at.size() >= 3) {
            be::F32 x = (be::F32)at[1].value();
            be::F32 y = (be::F32)at[2].value();

            oss.str("");
            oss << x << ", " << y;
            draw_text(oss.str(), vec2(bounds.x * 2.f / 3.f, bounds.y - text_bg_height + 2.f), Alignment::center, text_color, false);
         }
      }

      for (auto& child : *mod) {
         if (get_node_type(child) == node_type::n_fp_text) {
            if (child.size() >= 3) {
               if (child[1].text() == "reference"sv) {
                  S text(child[2].text());
                  if (text.empty()) {
                     oss.str("");
                     oss << child[2].value();
                     text = oss.str();
                  }
                  draw_text(text, vec2(2.f, bounds.y - text_bg_height + 2.f), Alignment::left, text_color, false);
               } else if (child[1].text() == "value"sv) {
                  S text(child[2].text());
                  if (text.empty()) {
                     oss.str("");
                     oss << child[2].value();
                     text = oss.str();
                  }
                  draw_text(text, vec2(bounds.x / 3.f, bounds.y - text_bg_height + 2.f), Alignment::center, text_color, false);
               }
            }
         }
      }

   } else if (highlight_modules_.empty() && highlight_nets_.size() == 1) {
      U32 net = *highlight_nets_.begin();
      
      auto pcb = find(root_, "kicad_pcb"sv);
      if (pcb != root_.end()) {
         for (const Node& child : *pcb) {
            if (get_node_type(child) == node_type::n_net) {
               if (child.size() >= 3 && (be::U32)child[1].value() == net) {
                  S text(child[2].text());
                  if (text.empty()) {
                     oss.str("");
                     oss << child[2].value();
                     text = oss.str();
                  }
                  draw_text(text, vec2(2.f, bounds.y - text_bg_height + 2.f), Alignment::left, text_color, false);
               }
            }
         }
      }
   }

   if (flipped_) {
      draw_text("B", bounds - vec2(52.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   } else {
      draw_text("F", bounds - vec2(52.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }
   
   if (see_thru_) {
      draw_text("T", bounds - vec2(42.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }

   if (!skip_silk_) {
      draw_text("S", bounds - vec2(32.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }

   if (!skip_copper_) {
      draw_text("C", bounds - vec2(22.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }

   if (skip_nets_.count(ground_net_) == 0) {
      draw_text("G", bounds - vec2(12.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }

   if (!skip_zones_) {
      draw_text("Z", bounds - vec2(2.f, text_bg_height - 2.f), Alignment::center, text_color, false);
   }

}
