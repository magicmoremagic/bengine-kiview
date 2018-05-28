#pragma once
#ifndef KIVIEW_DCEL_HPP_
#define KIVIEW_DCEL_HPP_

#include <be/core/be.hpp>
#include <be/core/glm.hpp>
#include <glm/vec2.hpp>
#include <boost/iterator/iterator_adaptor.hpp>
#include <vector>

///////////////////////////////////////////////////////////////////////////////

class Dcel final {
   struct edge {
      be::vec2 origin;
      be::U32 next;
      be::U32 prev;
      be::U32 twin;
   };
public:
   class iterator : boost::iterator_adaptor<iterator, std::vector<edge>::iterator, be::vec2> {
      friend class boost::iterator_core_access;
      friend class Dcel;
   public:
      iterator() { }

      iterator prev() {
         return iterator(*parent_, parent_->begin() + base_reference()->next);
      }

      iterator next() {
         return iterator(*parent_, parent_->begin() + base_reference()->next);
      }

      iterator twin() {
         return iterator(*parent_, parent_->begin() + base_reference()->next);
      }
      
   private:
      explicit iterator(std::vector<edge>& parent, const std::vector<edge>::iterator& iter)
         : iterator::iterator_adaptor_(iter),
           parent_(&parent) { }

      be::vec2& dereference() const {
         return base_reference()->origin;
      }

      std::vector<edge>* parent_;
   };




   iterator begin() {
      return iterator(edges_, edges_.begin());
   }
   iterator end() {
      return iterator(edges_, edges_.end());
   }

private:
   std::vector<edge> edges_;
};

#endif
