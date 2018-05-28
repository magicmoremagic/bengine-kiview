#pragma once
#ifndef KIVIEW_NODE_HPP_
#define KIVIEW_NODE_HPP_

#include <be/core/be.hpp>
#include <be/core/console.hpp>
#include <vector>
#include <be/util/string_interner.hpp>

///////////////////////////////////////////////////////////////////////////////
class Node {
public:
   using child_list = std::vector<Node>;
   using iterator = child_list::iterator;
   using const_iterator = child_list::const_iterator;
   using reverse_iterator = child_list::reverse_iterator;
   using const_reverse_iterator = child_list::const_reverse_iterator;

   enum class node_type {
      sexpr,
      text,
      value
   };

   Node() noexcept
      : type_(node_type::sexpr),
        value_(0) { }

   explicit Node(std::initializer_list<Node> il)
      : type_(node_type::sexpr),
        value_(0),
        children_(il) { }

   template <typename I>
   explicit Node(I begin, I end)
      : type_(node_type::sexpr),
        value_(0),
        children_(begin, end) {
   }

   explicit Node(be::SV text) noexcept
      : type_(node_type::text),
        text_(text),
        value_(0) { }

   explicit Node(be::F64 value) noexcept
      : type_(node_type::value),
        value_(value) { }

   node_type type() const noexcept {
      return type_;
   }

   be::SV text() const noexcept {
      return text_;
   }

   be::F64 value() const noexcept {
      return value_;
   }

   std::size_t size() const noexcept {
      return children_.size();
   }

   bool empty() const noexcept {
      return children_.empty();
   }

   iterator begin() {
      return children_.begin();
   }

   const_iterator begin() const {
      return children_.begin();
   }

   const_iterator cbegin() const {
      return children_.cbegin();
   }

   reverse_iterator rbegin() {
      return children_.rbegin();
   }

   const_reverse_iterator rbegin() const {
      return children_.rbegin();
   }

   const_reverse_iterator crbegin() const {
      return children_.crbegin();
   }

   iterator end() {
      return children_.end();
   }

   const_iterator end() const {
      return children_.end();
   }

   const_iterator cend() const {
      return children_.cend();
   }

   reverse_iterator rend() {
      return children_.rend();
   }

   const_reverse_iterator rend() const {
      return children_.rend();
   }

   const_reverse_iterator crend() const {
      return children_.crend();
   }

   Node& add(const Node& node) {
      children_.push_back(node);
      return children_.back();
   }

   Node& add(Node&& node) {
      children_.push_back(std::move(node));
      return children_.back();
   }

   Node& operator[](std::size_t index) {
      return children_[index];
   }

   const Node& operator[](std::size_t index) const {
      return children_[index];
   }

   Node& at(std::size_t index) {
      return children_.at(index);
   }

   const Node& at(std::size_t index) const {
      return children_.at(index);
   }

private:
   node_type type_;
   be::SV text_;
   be::F64 value_;
   child_list children_;
};

///////////////////////////////////////////////////////////////////////////////
inline Node::iterator find(Node& node, be::SV car) {
   using iterator = Node::iterator;
   for (iterator it = node.begin(), end = node.end(); it != end; ++it) {
      const Node& child = *it;
      if (child.type() == Node::node_type::sexpr && !child.empty()) {
         const Node& child_car = child[0];
         if (child_car.type() == Node::node_type::text && child_car.text() == car) {
            return it;
         }
      }
   }
   return node.end();
}

///////////////////////////////////////////////////////////////////////////////
inline Node::const_iterator find(const Node& node, be::SV car) {
   using iterator = Node::const_iterator;
   for (iterator it = node.begin(), end = node.end(); it != end; ++it) {
      const Node& child = *it;
      if (child.type() == Node::node_type::sexpr && !child.empty()) {
         const Node& child_car = child[0];
         if (child_car.type() == Node::node_type::text && child_car.text() == car) {
            return it;
         }
      }
   }
   return node.end();
}

///////////////////////////////////////////////////////////////////////////////
inline Node::iterator find(Node& node, be::F64 car) {
   using iterator = Node::iterator;
   for (iterator it = node.begin(), end = node.end(); it != end; ++it) {
      const Node& child = *it;
      if (child.type() == Node::node_type::sexpr && !child.empty()) {
         const Node& child_car = child[0];
         if (child_car.type() == Node::node_type::value && child_car.value() == car) {
            return it;
         }
      }
   }
   return node.end();
}

///////////////////////////////////////////////////////////////////////////////
inline Node::const_iterator find(const Node& node, be::F64 car) {
   using iterator = Node::const_iterator;
   for (iterator it = node.begin(), end = node.end(); it != end; ++it) {
      const Node& child = *it;
      if (child.type() == Node::node_type::sexpr && !child.empty()) {
         const Node& child_car = child[0];
         if (child_car.type() == Node::node_type::value && child_car.value() == car) {
            return it;
         }
      }
   }
   return node.end();
}

///////////////////////////////////////////////////////////////////////////////
Node parse(be::SV text, be::util::StringInterner& si) {
   using iterator = be::SV::const_iterator;

   Node root = Node();
   std::vector<Node*> stack { &root };

   bool in_number = false;
   bool in_fraction = false;
   bool in_string = false; // non-quoted; includes numbers
   bool in_quote = false;
   bool in_escape = false;

   be::S work;

   iterator begin = text.begin();
   iterator end = text.end();
   iterator it = begin;
   while (it != end) {
      char c = *it;
      ++it;

      if (in_number) {
         if (c >= '0' && c <= '9') {
            work.append(1, c);
            continue;
         } else if (!in_fraction && c == '.') {
            work.append(1, c);
            in_fraction = true;
            continue;
         } else {
            in_number = false;
            in_fraction = false;
            switch (c) {
               case ' ':
               case '\t':
               case '\r':
               case '\n':
               case '(':
               case ')':
                  stack.back()->add(Node(std::strtod(work.c_str(), nullptr)));
                  work.clear();
                  in_string = false;
                  --it; // so that we can reprocess '(' or ')' outside of a value state
                  continue;
               default:
                  assert(in_string);
                  break; // not continue!
            }
         }
      }

      if (in_string) {
         switch (c) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '(':
            case ')':
               stack.back()->add(Node(si(work)));
               work.clear();
               in_string = false;
               --it; // so that we can reprocess '(' or ')' outside of a string state
               continue;
            default:
               work.append(1, c);
               continue;
         }
      }

      if (in_quote) {
         if (in_escape) {
            switch (c) {
               case 'a': work.append(1, '\a'); break;
               case 'b': work.append(1, '\a'); break;
               case 'f': work.append(1, '\a'); break;
               case 'n': work.append(1, '\a'); break;
               case 'r': work.append(1, '\a'); break;
               case 't': work.append(1, '\a'); break;
               case 'v': work.append(1, '\a'); break;
               default: work.append(1, c); break;
            }
            in_escape = false;
            continue;
         } else if (c == '"') {
            if (it != end && *it == '"') {
               in_escape = true;
            } else {
               stack.back()->add(Node(si(work)));
               work.clear();
               in_quote = false;
               continue;
            }
         } else {
            work.append(1, c);
            continue;
         }
      }

      if ((c >= '0' && c <= '9') || c == '+' || c == '-') {
         work.append(1, c);
         in_number = true;
         in_string = true;
      } else if (c == '.') {
         work.append(1, c);
         in_number = true;
         in_fraction = true;
         in_string = true;
      } else if (c == '"') {
         in_quote = true;
      } else if (c == '(') {
         Node& sexpr = stack.back()->add(Node());
         stack.push_back(&sexpr);
      } else if (c == ')') {
         if (stack.size() > 1) {
            stack.pop_back();
         } else {
            work.append(1, c);
            in_string = true;
         }
      } else if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
         work.append(1, c);
         in_string = true;
      }
   }
   return root;
}

///////////////////////////////////////////////////////////////////////////////
std::ostream& operator<<(std::ostream& os, const Node& node) {
   auto config = be::get_ostream_config(os);
   switch (node.type()) {
      case Node::node_type::text:
         os << be::color::fg_white << node.text();
         break;
      case Node::node_type::value:
         os << be::color::fg_blue << node.value();
         break;
      case Node::node_type::sexpr:
      {
         bool simple = true;
         for (auto& child : node) {
            if (child.type() == Node::node_type::sexpr) {
               simple = false;
               break;
            }
         }
         if (simple) {
            os << be::color::fg_dark_gray << '(';
            for (auto& child : node) {
               os << ' ' << child;
            }
            os << ' ' << be::color::fg_dark_gray << ')';
         } else {
            os << be::color::fg_dark_gray << '(' << be::indent;
            for (auto& child : node) {
               os << be::nl << child;
            }
            os << be::unindent << be::nl << be::color::fg_dark_gray << ')';
         }
         break;
      }
      default:
         os << be::color::fg_dark_gray << "???";
   }
   be::set_ostream_config(os, config);
   return os;
}

#endif
