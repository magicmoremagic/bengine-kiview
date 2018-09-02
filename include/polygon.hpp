#pragma once
#ifndef KIVIEW_POLYGON_HPP_
#define KIVIEW_POLYGON_HPP_

#include "triangle.hpp"
#include <glm/vec2.hpp>
#include <vector>
#include <deque>

///////////////////////////////////////////////////////////////////////////////
struct edge {
   glm::vec2 origin; // edge is leaving this vertex
   edge* prev = nullptr;
   edge* next = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
void make_dcel(const std::vector<glm::vec2>& verts, std::deque<edge>& out);

///////////////////////////////////////////////////////////////////////////////
void make_dcel(std::deque<edge>::iterator begin, std::deque<edge>::iterator end);

///////////////////////////////////////////////////////////////////////////////
void triangulate_polygon(std::deque<edge>& edges, std::vector<triangle>& out);

///////////////////////////////////////////////////////////////////////////////
std::vector<triangle> triangulate_polygon(const std::vector<glm::vec2>& verts);

#endif
