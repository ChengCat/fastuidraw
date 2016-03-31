/*!
 * \file colorstop_atlas.cpp
 * \brief file colorstop_atlas.cpp
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


#include <vector>
#include <fastuidraw/colorstop_atlas.hpp>
#include "private/interval_allocator.hpp"
#include "private/util_private.hpp"

namespace
{
  class ColorInterpolator
  {
  public:
    ColorInterpolator(const fastuidraw::ColorStop &begin,
                      const fastuidraw::ColorStop &end):
      m_start(begin.m_place),
      m_coeff(1.0f / (end.m_place - begin.m_place)),
      m_startColor(begin.m_color),
      m_deltaColor(fastuidraw::vec4(end.m_color) - fastuidraw::vec4(begin.m_color))
    {}

    fastuidraw::u8vec4
    interpolate(float t)
    {
      float s;
      fastuidraw::vec4 value;

      s = (t - m_start) * m_coeff;
      s = std::min(1.0f, std::max(0.0f, s));
      value = m_startColor + s * m_deltaColor;

      return fastuidraw::u8vec4(value);
    };

  private:
    float m_start, m_coeff;
    fastuidraw::vec4 m_startColor, m_deltaColor;
  };

  class ColorStopAtlasPrivate
  {
  public:
    ColorStopAtlasPrivate(fastuidraw::ColorStopBackingStore::handle pbacking_store);

    void
    remove_entry_from_available_layers(std::map<int, std::set<int> >::iterator, int y);

    void
    add_bookkeeping(int new_size);

    mutable boost::mutex m_mutex;

    fastuidraw::ColorStopBackingStore::handle m_backing_store;
    int m_allocated;

    /* Each layer has an interval allocator to allocate
       and free "color stop arrays"
     */
    std::vector<fastuidraw::interval_allocator*> m_layer_allocator;

    /* m_available_layers[key] gives indices into m_layer_allocator
       for those layers for which largest_free_interval() returns
       key.
     */
    std::map<int, std::set<int> > m_available_layers;
  };

  class ColorStopBackingStorePrivate
  {
  public:
    ColorStopBackingStorePrivate(int w, int num_layers, bool presizable):
      m_dimensions(w, num_layers),
      m_width_times_height(m_dimensions.x() * m_dimensions.y()),
      m_resizeable(presizable)
    {}

    fastuidraw::ivec2 m_dimensions;
    int m_width_times_height;
    bool m_resizeable;
  };

  class ColorStopSequenceOnAtlasPrivate
  {
  public:
    fastuidraw::ColorStopAtlas::handle m_atlas;
    fastuidraw::ivec2 m_texel_location;
    int m_width;
    int m_start_slack, m_end_slack;
  };
}

////////////////////////////////////////
// ColorStopAtlasPrivate methods
ColorStopAtlasPrivate::
ColorStopAtlasPrivate(fastuidraw::ColorStopBackingStore::handle pbacking_store):
  m_backing_store(pbacking_store),
  m_allocated(0)
{
  assert(m_backing_store);
  add_bookkeeping(m_backing_store->dimensions().y());
}

void
ColorStopAtlasPrivate::
remove_entry_from_available_layers(std::map<int, std::set<int> >::iterator iter, int y)
{
  assert(iter != m_available_layers.end());
  assert(iter->second.find(y) != iter->second.end());
  iter->second.erase(y);
  if(iter->second.empty())
    {
      m_available_layers.erase(iter);
    }
}

void
ColorStopAtlasPrivate::
add_bookkeeping(int new_size)
{
  int width(m_backing_store->dimensions().x());
  int old_size(m_layer_allocator.size());
  std::set<int> &S(m_available_layers[width]);

  assert(new_size > old_size);
  m_layer_allocator.resize(new_size, NULL);
  for(int y = old_size; y < new_size; ++y)
    {
      m_layer_allocator[y] = FASTUIDRAWnew fastuidraw::interval_allocator(width);
      S.insert(y);
    }
}

/////////////////////////////////////
// fastuidraw::ColorStopBackingStore methods
fastuidraw::ColorStopBackingStore::
ColorStopBackingStore(int w, int num_layers, bool presizable)
{
  m_d = FASTUIDRAWnew ColorStopBackingStorePrivate(w, num_layers, presizable);
}

fastuidraw::ColorStopBackingStore::
ColorStopBackingStore(ivec2 wl, bool presizable)
{
  m_d = FASTUIDRAWnew ColorStopBackingStorePrivate(wl.x(), wl.y(), presizable);
}

fastuidraw::ColorStopBackingStore::
~ColorStopBackingStore()
{
  ColorStopBackingStorePrivate *d;
  d = reinterpret_cast<ColorStopBackingStorePrivate*>(m_d);
  FASTUIDRAWdelete(d);
  m_d = NULL;
}

fastuidraw::ivec2
fastuidraw::ColorStopBackingStore::
dimensions(void) const
{
  ColorStopBackingStorePrivate *d;
  d = reinterpret_cast<ColorStopBackingStorePrivate*>(m_d);
  return d->m_dimensions;
}

int
fastuidraw::ColorStopBackingStore::
width_times_height(void) const
{
  ColorStopBackingStorePrivate *d;
  d = reinterpret_cast<ColorStopBackingStorePrivate*>(m_d);
  return d->m_width_times_height;
}

bool
fastuidraw::ColorStopBackingStore::
resizeable(void) const
{
  ColorStopBackingStorePrivate *d;
  d = reinterpret_cast<ColorStopBackingStorePrivate*>(m_d);
  return d->m_resizeable;
}

void
fastuidraw::ColorStopBackingStore::
resize(int new_num_layers)
{
  ColorStopBackingStorePrivate *d;
  d = reinterpret_cast<ColorStopBackingStorePrivate*>(m_d);
  assert(d->m_resizeable);
  assert(new_num_layers > d->m_dimensions.y());
  resize_implement(new_num_layers);
  d->m_dimensions.y() = new_num_layers;
  d->m_width_times_height = d->m_dimensions.x() * d->m_dimensions.y();
}

///////////////////////////////////////
// fastuidraw::ColorStopAtlas methods
fastuidraw::ColorStopAtlas::
ColorStopAtlas(ColorStopBackingStore::handle pbacking_store)
{
  m_d = FASTUIDRAWnew ColorStopAtlasPrivate(pbacking_store);
}

fastuidraw::ColorStopAtlas::
~ColorStopAtlas()
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  assert(d->m_allocated == 0);
  for(std::vector<interval_allocator*>::iterator
        iter = d->m_layer_allocator.begin(),
        end = d->m_layer_allocator.end();
      iter != end; ++iter)
    {
      FASTUIDRAWdelete(*iter);
    }
  FASTUIDRAWdelete(d);
  m_d = NULL;
}

void
fastuidraw::ColorStopAtlas::
flush(void) const
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  autolock_mutex m(d->m_mutex);
  d->m_backing_store->flush();
}

int
fastuidraw::ColorStopAtlas::
total_available(void) const
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  autolock_mutex m(d->m_mutex);
  return d->m_backing_store->width_times_height() - d->m_allocated;
}

int
fastuidraw::ColorStopAtlas::
largest_allocation_possible(void) const
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  autolock_mutex m(d->m_mutex);
  if(d->m_available_layers.empty())
    {
      return 0;
    }

  return d->m_available_layers.rbegin()->first;
}

void
fastuidraw::ColorStopAtlas::
deallocate(ivec2 location, int width)
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  autolock_mutex m(d->m_mutex);
  int y(location.y());
  assert(d->m_layer_allocator[y]);

  int old_max, new_max;

  old_max = d->m_layer_allocator[y]->largest_free_interval();
  d->m_layer_allocator[y]->free_interval(location.x(), width);
  new_max = d->m_layer_allocator[y]->largest_free_interval();

  if(old_max != new_max)
    {
      std::map<int, std::set<int> >::iterator iter;

      iter = d->m_available_layers.find(old_max);
      d->remove_entry_from_available_layers(iter, y);
      d->m_available_layers[new_max].insert(y);
    }
  d->m_allocated -= width;
}

fastuidraw::ivec2
fastuidraw::ColorStopAtlas::
allocate(const_c_array<u8vec4> data)
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);

  autolock_mutex m(d->m_mutex);

  std::map<int, std::set<int> >::iterator iter;
  ivec2 return_value;
  int width(data.size());

  assert(width > 0);
  assert(width <= max_width());

  iter = d->m_available_layers.lower_bound(width);
  if(iter == d->m_available_layers.end())
    {
      if(d->m_backing_store->resizeable())
        {
          /* TODO: what should the resize algorithm be?
             Right now we double the size, but that might
             be excessive.
           */
          int new_size, old_size;
          old_size = d->m_backing_store->dimensions().y();
          new_size = std::max(1, old_size * 2);
          d->m_backing_store->resize(new_size);
          d->add_bookkeeping(new_size);

          iter = d->m_available_layers.lower_bound(width);
          assert(iter != d->m_available_layers.end());
        }
      else
        {
          assert(!"ColorStop atlas exhausted");
          return ivec2(-1, -1);
        }
    }

  assert(!iter->second.empty());

  int y(*iter->second.begin());
  int old_max, new_max;

  old_max = d->m_layer_allocator[y]->largest_free_interval();
  return_value.x() = d->m_layer_allocator[y]->allocate_interval(width);
  assert(return_value.x() >= 0);
  new_max = d->m_layer_allocator[y]->largest_free_interval();

  if(old_max != new_max)
    {
      d->remove_entry_from_available_layers(iter, y);
      d->m_available_layers[new_max].insert(y);
    }
  return_value.y() = y;

  d->m_backing_store->set_data(return_value.x(), return_value.y(),
                               width, data);
  d->m_allocated += width;
  return return_value;
}


int
fastuidraw::ColorStopAtlas::
max_width(void) const
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);
  return d->m_backing_store->dimensions().x();
}

fastuidraw::ColorStopBackingStore::const_handle
fastuidraw::ColorStopAtlas::
backing_store(void) const
{
  ColorStopAtlasPrivate *d;
  d = reinterpret_cast<ColorStopAtlasPrivate*>(m_d);
  return d->m_backing_store;
}

///////////////////////////////////////////
// fastuidraw::ColorStopSequenceOnAtlas methods
fastuidraw::ColorStopSequenceOnAtlas::
ColorStopSequenceOnAtlas(const ColorStopSequence &pcolor_stops,
                         ColorStopAtlas::handle atlas,
                         int pwidth)
{
  ColorStopSequenceOnAtlasPrivate *d;
  d = FASTUIDRAWnew ColorStopSequenceOnAtlasPrivate();
  m_d = d;

  d->m_atlas = atlas;
  d->m_width = pwidth;

  const_c_array<ColorStop> color_stops(pcolor_stops.values());
  assert(d->m_atlas);
  assert(pwidth>0);

  if(pwidth >= d->m_atlas->max_width())
    {
      d->m_width = d->m_atlas->max_width();
      d->m_start_slack = 0;
      d->m_end_slack = 0;
    }
  else if(pwidth == d->m_atlas->max_width() - 1)
    {
      d->m_start_slack = 0;
      d->m_end_slack = 1;
    }
  else
    {
      d->m_start_slack = 1;
      d->m_end_slack = 1;
    }

  std::vector<u8vec4> data(d->m_width + d->m_start_slack + d->m_end_slack);

  /* Discretize and interpolate color_stops into data
   */
  {
    unsigned int data_i, color_stops_i;
    float current_t, delta_t;

    delta_t = 1.0f / static_cast<float>(d->m_width);
    current_t = static_cast<float>(-d->m_start_slack) * delta_t;

    for(data_i = 0; current_t <= color_stops[0].m_place; ++data_i, current_t += delta_t)
      {
        data[data_i] = color_stops[0].m_color;
      }

    for(color_stops_i = 1;  color_stops_i < color_stops.size(); ++color_stops_i)
      {
        ColorStop prev_color(color_stops[color_stops_i-1]);
        ColorStop next_color(color_stops[color_stops_i]);
        ColorInterpolator color_interpolate(prev_color, next_color);

        for(; current_t <= next_color.m_place && data_i < data.size();
            ++data_i, current_t += delta_t)
          {
            data[data_i] = color_interpolate.interpolate(current_t);
          }
      }

    for(;data_i < data.size(); ++data_i)
      {
        data[data_i] = color_stops.back().m_color;
      }
  }


  d->m_texel_location = d->m_atlas->allocate(make_c_array(data));

  /* Adjust m_texel_location to remove the start slack
   */
  d->m_texel_location.x() += d->m_start_slack;
}

fastuidraw::ColorStopSequenceOnAtlas::
~ColorStopSequenceOnAtlas(void)
{
  ColorStopSequenceOnAtlasPrivate *d;
  d = reinterpret_cast<ColorStopSequenceOnAtlasPrivate*>(m_d);

  ivec2 loc(d->m_texel_location);

  loc.x() -= d->m_start_slack;
  d->m_atlas->deallocate(loc, d->m_width + d->m_start_slack + d->m_end_slack);
  FASTUIDRAWdelete(d);
  m_d = NULL;
}

fastuidraw::ivec2
fastuidraw::ColorStopSequenceOnAtlas::
texel_location(void) const
{
  ColorStopSequenceOnAtlasPrivate *d;
  d = reinterpret_cast<ColorStopSequenceOnAtlasPrivate*>(m_d);
  return d->m_texel_location;
}

int
fastuidraw::ColorStopSequenceOnAtlas::
width(void) const
{
  ColorStopSequenceOnAtlasPrivate *d;
  d = reinterpret_cast<ColorStopSequenceOnAtlasPrivate*>(m_d);
  return d->m_width;
}

fastuidraw::ColorStopAtlas::const_handle
fastuidraw::ColorStopSequenceOnAtlas::
atlas(void) const
{
  ColorStopSequenceOnAtlasPrivate *d;
  d = reinterpret_cast<ColorStopSequenceOnAtlasPrivate*>(m_d);
  return d->m_atlas;
}