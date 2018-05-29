#include "kiview_app.hpp"
#include "node.hpp"
#include "polygon.hpp"

#include <be/core/logging.hpp>
#include <be/core/version.hpp>
#include <be/core/stack_trace.hpp>
#include <be/core/alg.hpp>
#include <be/util/keyword_parser.hpp>
#include <be/util/get_file_contents.hpp>
#include <be/platform/glfw_window.hpp>
#include <be/gfx/version.hpp>
#include <be/cli/cli.hpp>
#include <be/gfx/bgl.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>
#include <sstream>
#include <iostream>
#include <string>

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
F32 get_scalar(const Node& node, SV which) {
   Node::const_iterator it = find(node, which);
   if (it != node.end()) {
      const Node& point = *it;
      if (point.size() >= 2) {
         return (F32)point[1].value();
      }
   }
   return 0.f;
}

///////////////////////////////////////////////////////////////////////////////
vec2 get_point(const Node& node, SV which) {
   Node::const_iterator it = find(node, which);
   if (it != node.end()) {
      const Node& point = *it;
      if (point.size() >= 3) {
         return vec2(dvec2(point[1].value(), point[2].value()));
      }
   }
   return vec2();
}

///////////////////////////////////////////////////////////////////////////////
vec4 get_layer_color(const Node& node) {
   Node::const_iterator it = find(node, "layer");
   if (it != node.end()) {
      const Node& layer = *it;
      if (layer.size() >= 2) {
         constexpr SV edge_cuts = "Edge.Cuts";
         constexpr SV front_copper = "F.Cu";
         constexpr SV back_copper = "B.Cu";

         SV layer_name = layer[1].text();
         if (layer_name == edge_cuts) {
            return vec4(0.5f, 0.5f, 1.f, 1.f);
         } else if (layer_name == front_copper) {
            return vec4(0.5f, 0.f, 0.f, 1.f);
            //return vec4();
         } else if (layer_name == back_copper) {
            return vec4(0.f, 0.5f, 0.f, 1.f);
         }
      }
   }
   return vec4(1.f, 1.f, 1.f, 0.5f);
}
} // ::()

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::run_() {
   S file = be::util::get_text_file_contents_string(filename_);
   util::StringInterner si;
   si.provisioning_policy([](std::size_t s) { return min(s * 2, 0x1000000ull) + 0x10000; });
   Node root = parse(file, si);
   Node empty;
   Node::const_iterator iter = find(root, "kicad_pcb");
   const Node& pcb = iter == root.end() ? empty : *iter;

   S window_title = filename_;

   Node::const_iterator title_block_it = find(pcb, "title_block");
   if (title_block_it != pcb.end()) {
      Node::const_iterator title_it = find(*title_block_it, "title");
      if (title_it != title_block_it->end()) {
         const Node& title = *title_it;
         if (title.size() >= 2) {
            window_title = title[1].text();
         }
      }
   }

   board_bounds_ = get_area(pcb);

   be_info() << "Board Bounds"
         & attr("Horizontal") << board_bounds_.left() << " - " << board_bounds_.right()
         & attr("Vertical") << board_bounds_.bottom() << " - " << board_bounds_.top()
         | default_log();

   window_title = "KiView - " + window_title;
   glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
   glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
   GLFWwindow* wnd = glfwCreateWindow(viewport_.x, viewport_.y, window_title.c_str(), nullptr, nullptr);
   glfwMakeContextCurrent(wnd);
   glfwSwapInterval(1);
   glfwSetWindowUserPointer(wnd, this);

   gl::init_context();

   if (GL_KHR_debug) {
      //#bgl checked(GL_KHR_debug)
      glEnable(GL_DEBUG_OUTPUT);
      glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
      glDebugMessageCallback(check_errors, nullptr);
      //#bgl unchecked
   }

   glViewport(0, 0, viewport_.x, viewport_.y);
   autoscale_();

   glClearColor(0, 0, 0, 0);
   glEnable(GL_BLEND);
   glBlendEquation(GL_FUNC_ADD);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE);
   glEnable(GL_BLEND);

   glfwSetWindowSizeCallback(wnd, [](GLFWwindow* wnd, int w, int h) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      ivec2 new_size { w, h };

      glViewport(0, 0, new_size.x, new_size.y);

      if (new_size != app.viewport_ && new_size.x * new_size.y > 0) {
         app.viewport_ = new_size;
         app.autoscale_();
         glfwPostEmptyEvent();
      }
   });

   glfwSetScrollCallback(wnd, [](GLFWwindow* wnd, double x, double y) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      app.scale_ *= (F32)std::pow(1.2, y);
      app.enable_autoscale_ = false;
   });

   glfwSetCursorPosCallback(wnd, [](GLFWwindow* wnd, double x, double y) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      vec2 offset = vec2(dvec2(x, y)) - vec2(app.viewport_) / 2.f;
      vec2 scaled_offset = offset / app.scale_;

      if (app.dragging_) {
         app.center_ -= scaled_offset - app.relative_cursor_;
         app.enable_autocenter_ = false;
      }

      app.relative_cursor_ = scaled_offset;
      app.cursor_ = app.center_ + scaled_offset;
   });

   glfwSetMouseButtonCallback(wnd, [](GLFWwindow* wnd, int btn, int action, int mods) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      if (btn == GLFW_MOUSE_BUTTON_3) {
         if (action == GLFW_RELEASE) {
            app.dragging_ = false;
         } else {
            app.dragging_ = true;
         }
      }
   });

   glfwSetKeyCallback(wnd, [](GLFWwindow* wnd, int key, int scancode, int action, int mods) {
      KiViewApp& app = *static_cast<KiViewApp*>(glfwGetWindowUserPointer(wnd));
      if (action == GLFW_RELEASE) {
         if (key == GLFW_KEY_HOME) {
            app.enable_autoscale_ = true;
            app.enable_autocenter_ = true;
            app.autoscale_();
         }
      }
   });

   while (!glfwWindowShouldClose(wnd)) {
      glfwWaitEvents();

      glClear(GL_COLOR_BUFFER_BIT);

      mat4 proj = glm::ortho(0.f, (F32)viewport_.x, (F32)viewport_.y, 0.f);
      mat4 view = glm::translate(
         glm::scale(
         glm::translate(
         mat4(),
         vec3(viewport_, 0.f) / 2.f),
         vec3(scale_)),
         vec3(-center_, 0.f));

      glMatrixMode(GL_PROJECTION);
      glLoadMatrixf(glm::value_ptr(proj));

      glMatrixMode(GL_MODELVIEW);
      glLoadMatrixf(glm::value_ptr(view));

      glBegin(GL_LINES);
      glColor3f(0.25f, 0.f, 0.f);
      glVertex2fv(glm::value_ptr(board_bounds_.top_left()));
      glVertex2fv(glm::value_ptr(board_bounds_.top_right()));
      glVertex2fv(glm::value_ptr(board_bounds_.bottom_left()));
      glVertex2fv(glm::value_ptr(board_bounds_.bottom_right()));
      glVertex2fv(glm::value_ptr(board_bounds_.top_left()));
      glVertex2fv(glm::value_ptr(board_bounds_.bottom_left()));
      glVertex2fv(glm::value_ptr(board_bounds_.top_right()));
      glVertex2fv(glm::value_ptr(board_bounds_.bottom_right()));
      glEnd();

      constexpr SV module_tag = "module";
      constexpr SV gr_line_tag = "gr_line";
      constexpr SV gr_arc_tag = "gr_arc";
      constexpr SV segment_tag = "segment";
      constexpr SV zone_tag = "zone";

      for (auto& node : pcb) {
         if (node.empty() || node[0].type() != Node::node_type::text) {
            continue;
         }

         SV type = node[0].text();

         if (type == module_tag) {
            glPushMatrix();

            vec2 at = get_point(node, "at");
            glTranslated(at.x, at.y, 0);

            glBegin(GL_LINES);
            glColor3f(1.f, 1.f, 1.f);
            //glVertex2f(-3.f, -3.f);
            //glVertex2f(3.f, 3.f);
            //glVertex2f(-3.f, 3.f);
            //glVertex2f(3.f, -3.f);
            glEnd();

            glPopMatrix();
         } else if (type == segment_tag) {
            vec2 start = get_point(node, "start");
            vec2 end = get_point(node, "end");
            vec4 color = get_layer_color(node);

            glBegin(GL_LINES);
            glColor4fv(glm::value_ptr(color));
            glVertex2fv(glm::value_ptr(start));
            glVertex2fv(glm::value_ptr(end));
            glEnd();
         } else if (type == gr_line_tag) {
            vec2 start = get_point(node, "start");
            vec2 end = get_point(node, "end");
            vec4 color = get_layer_color(node);

            glBegin(GL_LINES);
            glColor4fv(glm::value_ptr(color));
            glVertex2fv(glm::value_ptr(start));
            glVertex2fv(glm::value_ptr(end));
            glEnd();
         } else if (type == gr_arc_tag) {
            vec2 center = get_point(node, "start");
            vec2 tangent_point = get_point(node, "end");
            vec4 color = get_layer_color(node);
            F32 angle_deg = get_scalar(node, "angle");
            const F32 angle = glm::radians(angle_deg);
            const U32 segments = max(2u, (U32)(angle_deg / 5));

            vec2 tangent_point_delta = tangent_point - center;

            mat2 cob = mat2(tangent_point_delta,
               vec2(-tangent_point_delta.y, tangent_point_delta.x));

            glBegin(GL_LINES);
            glColor4fv(glm::value_ptr(color));

            vec2 last = tangent_point;

            for (U32 s = 1; s < segments; ++s) {
               F32 theta = angle * s / (F32)(segments - 1);
               vec2 point = cob * vec2(cos(theta), sin(theta)) + center;

               glVertex2fv(glm::value_ptr(last));
               glVertex2fv(glm::value_ptr(point));

               last = point;
            }

            glEnd();
         } else if (type == zone_tag) {
            vec4 color = get_layer_color(node);
            if (color == vec4()) {
               continue;
            }
            constexpr SV filled_polygon_tag = "filled_polygon";
            constexpr SV xy_tag = "xy";
            for (const Node& subnode : node) {
               if (!subnode.empty() && subnode[0].text() == filled_polygon_tag) {
                  Node::const_iterator pts_it = find(subnode, "pts");
                  if (pts_it != subnode.end()) {
                     std::vector<vec2> points;
                     const Node& pts = *pts_it;
                     for (const Node& pt : pts) {
                        if (pt.size() >= 3 && pt[0].text() == xy_tag) {
                           points.push_back(vec2(dvec2(pt[1].value(), pt[2].value())));
                        }
                     }
                     std::vector<triangle> tris = triangulate_polygon(points);

                     glColor4fv(glm::value_ptr(color));
                     
                     glBegin(GL_TRIANGLES);
                     for (triangle& tri : tris) {
                        glVertex2fv(glm::value_ptr(tri.v[0]));
                        glVertex2fv(glm::value_ptr(tri.v[1]));
                        glVertex2fv(glm::value_ptr(tri.v[2]));
                     }
                     glEnd();
                     
                  }
               }
            }
         }
      }

      glfwSwapBuffers(wnd);
   }

   glfwDestroyWindow(wnd);
}

///////////////////////////////////////////////////////////////////////////////
void KiViewApp::autoscale_() {
   if (enable_autocenter_) {
      center_ = board_bounds_.center();
   }

   if (enable_autoscale_) {
      vec2 scale = vec2(viewport_) * 0.98f / board_bounds_.dim;
      scale_ = min(scale.x, scale.y);

      std::cout << (25.4f * scale_) << " ppi" << nl;
   }
}
