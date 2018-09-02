#pragma once
#ifndef KIVIEW_RENDER_LAYER_HPP_
#define KIVIEW_RENDER_LAYER_HPP_

#include "node.hpp"
#include "triangle.hpp"
#include <functional>
#include <vector>

using RenderNodePredicate = std::function<std::pair<bool, bool>(const Node&, const std::vector<const Node*>&)>;

//////////////////////////////////////////////////////////////////////////////
be::U32 pad_segment_density();
void pad_segment_density(be::U32 segments_per_circle);

//////////////////////////////////////////////////////////////////////////////
be::U32 endcap_segment_density();
void endcap_segment_density(be::U32 segments_per_circle);

//////////////////////////////////////////////////////////////////////////////
be::U32 arc_segment_density();
void arc_segment_density(be::U32 segments_per_circle);

//////////////////////////////////////////////////////////////////////////////
be::U32 zone_perimeter_endcap_segment_density();
void zone_perimeter_endcap_segment_density(be::U32 segments_per_circle);

//////////////////////////////////////////////////////////////////////////////
std::vector<triangle> render_layer(const Node& node, const RenderNodePredicate& pred);

#endif
