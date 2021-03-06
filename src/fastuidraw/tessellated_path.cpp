/*!
 * \file tessellated_path.cpp
 * \brief file tessellated_path.cpp
 *
 * Copyright 2016 by Intel.
 *
 * Contact: kevin.rogovin@intel.com
 *
 * This Source Code Form is subject to the
 * terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with
 * this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 *
 * \author Kevin Rogovin <kevin.rogovin@intel.com>
 *
 */


#include <list>
#include <vector>
#include <fastuidraw/tessellated_path.hpp>
#include <fastuidraw/path.hpp>
#include <fastuidraw/painter/stroked_path.hpp>
#include <fastuidraw/painter/filled_path.hpp>
#include "private/util_private.hpp"

namespace
{
  class TessellatedPathPrivate
  {
  public:
    TessellatedPathPrivate(const fastuidraw::Path &input,
                           fastuidraw::TessellatedPath::TessellationParams TP);

    std::vector<std::vector<fastuidraw::range_type<unsigned int> > > m_edge_ranges;
    std::vector<fastuidraw::TessellatedPath::point> m_point_data;
    fastuidraw::vec2 m_box_min, m_box_max;
    fastuidraw::TessellatedPath::TessellationParams m_params;
    fastuidraw::vecN<float, fastuidraw::TessellatedPath::number_threshholds> m_effective_threshholds;
    unsigned int m_max_segments;
    fastuidraw::reference_counted_ptr<const fastuidraw::StrokedPath> m_stroked;
    fastuidraw::reference_counted_ptr<const fastuidraw::FilledPath> m_filled;
  };
}

//////////////////////////////////////////////
// TessellatedPathPrivate methods
TessellatedPathPrivate::
TessellatedPathPrivate(const fastuidraw::Path &input,
                       fastuidraw::TessellatedPath::TessellationParams TP):
  m_edge_ranges(input.number_contours()),
  m_box_min(0.0f, 0.0f),
  m_box_max(0.0f, 0.0f),
  m_params(TP),
  m_effective_threshholds(0.0f),
  m_max_segments(0u)
{
  using namespace fastuidraw;

  if (input.number_contours() > 0)
    {
      std::list<std::vector<TessellatedPath::point> > temp;
      std::vector<TessellatedPath::point> work_room;
      std::list<std::vector<TessellatedPath::point> >::const_iterator iter, end_iter;

      for(unsigned int loc = 0, o = 0, endo = input.number_contours(); o < endo; ++o)
        {
          reference_counted_ptr<const PathContour> contour(input.contour(o));
          float contour_length(0.0f), open_contour_length(0.0f), closed_contour_length(0.0f);
          std::list<std::vector<TessellatedPath::point> >::iterator start_contour;

          m_edge_ranges[o].resize(contour->number_points());
          for(unsigned int e = 0, ende = contour->number_points(); e < ende; ++e)
            {
              unsigned int needed;
              vecN<float, TessellatedPath::number_threshholds> tmp;

              work_room.resize(m_params.m_max_segments + 1);
              temp.push_back(std::vector<TessellatedPath::point>());
              needed = contour->interpolator(e)->produce_tessellation(m_params,
                                                                      make_c_array(work_room),
                                                                      tmp);
              m_edge_ranges[o][e] = range_type<unsigned int>(loc, loc + needed);
              loc += needed;

              FASTUIDRAWassert(needed > 0u);
              m_max_segments = t_max(m_max_segments, needed - 1);
              for (unsigned int i = 0; i < TessellatedPath::number_threshholds; ++i)
                {
                  m_effective_threshholds[i] = t_max(m_effective_threshholds[i], tmp[i]);
                }

              work_room.resize(needed);
              for(unsigned int n = 0; n < work_room.size(); ++n)
                {
                  const vec2 &pt(work_room[n].m_p);

                  work_room[n].m_distance_from_contour_start
                    = contour_length + work_room[n].m_distance_from_edge_start;

                  if (o == 0 && e == 0 && n == 0)
                    {
                      m_box_min = pt;
                      m_box_max = pt;
                    }
                  else
                    {
                      m_box_min.x() = t_min(m_box_min.x(), pt.x());
                      m_box_min.y() = t_min(m_box_min.y(), pt.y());
                      m_box_max.x() = t_max(m_box_max.x(), pt.x());
                      m_box_max.y() = t_max(m_box_max.y(), pt.y());
                    }
                }

              std::list<std::vector<TessellatedPath::point> >::iterator t;
              t = temp.insert(temp.end(), work_room);
              if (e == 0)
                {
                  start_contour = t;
                }

              contour_length = temp.back().back().m_distance_from_contour_start;

              if (e + 2 == ende)
                {
                  open_contour_length = contour_length;
                }
              else if (e + 1 == ende)
                {
                  closed_contour_length = contour_length;
                }

              for(unsigned int n = 0; n < needed; ++n)
                {
                  (*t)[n].m_edge_length = (*t)[needed - 1].m_distance_from_edge_start;
                }
            }

          for(std::list<std::vector<fastuidraw::TessellatedPath::point> >::iterator
                t = start_contour, endt = temp.end(); t != endt; ++t)
            {
              for(unsigned int e = 0, ende = t->size(); e < ende; ++e)
                {
                  (*t)[e].m_open_contour_length = open_contour_length;
                  (*t)[e].m_closed_contour_length = closed_contour_length;
                }
            }
        }

      unsigned int total_needed;

      total_needed = m_edge_ranges.back().back().m_end;
      m_point_data.reserve(total_needed);
      for(iter = temp.begin(), end_iter = temp.end(); iter != end_iter; ++iter)
        {
          std::copy(iter->begin(), iter->end(), std::back_inserter(m_point_data));
        }
      FASTUIDRAWassert(total_needed == m_point_data.size());
    }
  else
    {
      m_box_min = m_box_max = fastuidraw::vec2(0.0f, 0.0f);
    }
}

//////////////////////////////////////
// fastuidraw::TessellatedPath methods
fastuidraw::TessellatedPath::
TessellatedPath(const Path &input,
                fastuidraw::TessellatedPath::TessellationParams TP)
{
  m_d = FASTUIDRAWnew TessellatedPathPrivate(input, TP);
}

fastuidraw::TessellatedPath::
~TessellatedPath()
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);
  FASTUIDRAWdelete(d);
  m_d = nullptr;
}

const fastuidraw::reference_counted_ptr<const fastuidraw::StrokedPath>&
fastuidraw::TessellatedPath::
stroked(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);
  if (!d->m_stroked)
    {
      d->m_stroked = FASTUIDRAWnew StrokedPath(*this);
    }
  return d->m_stroked;
}

const fastuidraw::reference_counted_ptr<const fastuidraw::FilledPath>&
fastuidraw::TessellatedPath::
filled(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);
  if (!d->m_filled)
    {
      d->m_filled = FASTUIDRAWnew FilledPath(*this);
    }
  return d->m_filled;
}

const fastuidraw::TessellatedPath::TessellationParams&
fastuidraw::TessellatedPath::
tessellation_parameters(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_params;
}

float
fastuidraw::TessellatedPath::
effective_threshhold(enum threshhold_type_t tp) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);
  return tp < d->m_effective_threshholds.size() ?
    d->m_effective_threshholds[tp] : 0.0f;
}

unsigned int
fastuidraw::TessellatedPath::
max_segments(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);
  return d->m_max_segments;
}

fastuidraw::c_array<const fastuidraw::TessellatedPath::point>
fastuidraw::TessellatedPath::
point_data(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return make_c_array(d->m_point_data);
}

unsigned int
fastuidraw::TessellatedPath::
number_contours(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_edge_ranges.size();
}

fastuidraw::range_type<unsigned int>
fastuidraw::TessellatedPath::
contour_range(unsigned int contour) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return range_type<unsigned int>(d->m_edge_ranges[contour].front().m_begin,
                                  d->m_edge_ranges[contour].back().m_end);
}

fastuidraw::range_type<unsigned int>
fastuidraw::TessellatedPath::
unclosed_contour_range(unsigned int contour) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  range_type<unsigned int> return_value;
  unsigned int num_edges(number_edges(contour));

  return_value.m_begin = d->m_edge_ranges[contour].front().m_begin;
  return_value.m_end = (num_edges > 1) ?
    d->m_edge_ranges[contour][num_edges - 2].m_end:
    d->m_edge_ranges[contour][num_edges - 1].m_end;

  return return_value;
}

fastuidraw::c_array<const fastuidraw::TessellatedPath::point>
fastuidraw::TessellatedPath::
contour_point_data(unsigned int contour) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return make_c_array(d->m_point_data).sub_array(contour_range(contour));
}

fastuidraw::c_array<const fastuidraw::TessellatedPath::point>
fastuidraw::TessellatedPath::
unclosed_contour_point_data(unsigned int contour) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return make_c_array(d->m_point_data).sub_array(unclosed_contour_range(contour));
}

unsigned int
fastuidraw::TessellatedPath::
number_edges(unsigned int contour) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_edge_ranges[contour].size();
}

fastuidraw::range_type<unsigned int>
fastuidraw::TessellatedPath::
edge_range(unsigned int contour, unsigned int edge) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_edge_ranges[contour][edge];
}

fastuidraw::c_array<const fastuidraw::TessellatedPath::point>
fastuidraw::TessellatedPath::
edge_point_data(unsigned int contour, unsigned int edge) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return make_c_array(d->m_point_data).sub_array(edge_range(contour, edge));
}

fastuidraw::vec2
fastuidraw::TessellatedPath::
bounding_box_min(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_box_min;
}

fastuidraw::vec2
fastuidraw::TessellatedPath::
bounding_box_max(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_box_max;
}

fastuidraw::vec2
fastuidraw::TessellatedPath::
bounding_box_size(void) const
{
  TessellatedPathPrivate *d;
  d = static_cast<TessellatedPathPrivate*>(m_d);

  return d->m_box_max - d->m_box_min;
}
