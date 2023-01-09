//     *************************** LiveVisionKit ****************************
//     Copyright (C) 2022  Sebastian Di Marco (crowsinc.dev@gmail.com)
//
//     This program is free software: you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation, either version 3 of the License, or
//     (at your option) any later version.
//
//     This program is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License
//     along with this program.  If not, see <https://www.gnu.org/licenses/>.
//     **********************************************************************

#include "SpatialMap.hpp"

#include "Diagnostics/Directives.hpp"
#include "Math/Math.hpp"

#include <array>

namespace lvk
{
//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::SpatialMap(const cv::Size resolution)
    {
        rescale(resolution);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::SpatialMap(const cv::Size resolution, const cv::Rect input_region)
    {
        rescale(resolution, input_region);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline void SpatialMap<T>::rescale(const cv::Size resolution)
    {
        rescale(resolution, resolution);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline void SpatialMap<T>::rescale(const cv::Size resolution, const cv::Rect input_region)
    {
        LVK_ASSERT(resolution.width >= 1);
        LVK_ASSERT(resolution.height >= 1);
        LVK_ASSERT(input_region.width >= resolution.width);
        LVK_ASSERT(input_region.height >= resolution.height);

        if(resolution != m_MapResolution || input_region != m_InputRegion)
        {
            m_InputRegion = input_region;
            m_MapResolution = resolution;

            // Spatial size of each key within the input region.
            m_KeySize.width = static_cast<float>(m_InputRegion.width) / static_cast<float>(m_MapResolution.width);
            m_KeySize.height = static_cast<float>(m_InputRegion.height) / static_cast<float>(m_MapResolution.height);

            m_Map.clear();
            m_Map.resize(m_MapResolution.area(), m_EmptySymbol);

            // Re-insert all the elements such that they keep the same key.
            std::vector<std::pair<spatial_key, T>> old_data = std::move(m_Data);
            for(auto& [key, item] : old_data)
                if(is_key_valid(key))
                    place_at(key, item);
        }
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline T& SpatialMap<T>::place_at(const spatial_key key, const T& item)
    {
        LVK_ASSERT(is_key_valid(key));

        // If the key is empty, generate a new pointer,
        // otherwise we just replace the existing item.

        size_t& data_link = fetch_data_link(key);
        if(is_data_link_empty(data_link))
        {
            data_link = m_Data.size();
            return m_Data.emplace_back(key, item).second;
        }
        else return m_Data.emplace(m_Data.begin() + data_link, key, item)->second;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename... Args>
    inline T& SpatialMap<T>::emplace_at(const spatial_key key, Args... args)
    {
        LVK_ASSERT(is_key_valid(key));

        // If the key is empty, generate a new pointer,
        // otherwise we just replace the existing item.

        // TODO: this isn't much of an optimization compared to the
        // place function, maybe there is something more we can do?

        size_t& data_link = fetch_data_link(key);
        if(is_data_link_empty(data_link))
        {
            data_link = m_Data.size();
            return m_Data.emplace_back(key, T{args...}).second;
        }
        else return m_Data.emplace(m_Data.begin() + data_link, key, T{args...})->second;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline T& SpatialMap<T>::place(const cv::Point_<P>& position, const T& item)
    {
        LVK_ASSERT(within_bounds(position));

        return place_at(key_of(position), item);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline bool SpatialMap<T>::try_place(const cv::Point_<P>& position, const T& item)
    {
        if(within_bounds(position))
        {
            place(position, item);
            return true;
        }
        return false;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P, typename... Args>
    inline T& SpatialMap<T>::emplace(const cv::Point_<P>& position, Args... args)
    {
        LVK_ASSERT(within_bounds(position));

        return emplace_at(key_of(position), args...);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P, typename... Args>
    inline bool SpatialMap<T>::try_emplace(const cv::Point_<P>& position, Args... args)
    {
        if(within_bounds(position))
        {
            emplace(position, args...);
            return true;
        }
        return false;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline void SpatialMap<T>::remove(const spatial_key key)
    {
        LVK_ASSERT(contains(key));

        // To remove an item quickly we will swap it with the last added
        // item, which will be at the end of the data vector. We can then
        // pop it off without having to shuffle any items. The item which
        // used to be last will have its data pointer adjusted as necessary.

        size_t& item_data_link = fetch_data_link(key);

        const auto& [replace_key, replace_item] = m_Data.back();
        if(key != replace_key)
        {
            // Swap the replacement item to the new position
            std::swap(m_Data[item_data_link], replace_item);

            size_t& replace_data_link = fetch_data_link(key);
            replace_data_link = item_data_link;
        }

        // Remove the requested item
        m_Data.pop_back();
        clear_data_link(item_data_link);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline bool SpatialMap<T>::try_remove(const spatial_key key)
    {
        if(contains(key))
        {
            remove(key);
            return true;
        }
        return false;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline T& SpatialMap<T>::at(const spatial_key key)
    {
        LVK_ASSERT(contains(key));

        return m_Data[fetch_data_link(key)].second;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const T& SpatialMap<T>::at(const spatial_key key) const
    {
        LVK_ASSERT(contains(key));

        return m_Data[fetch_data_link(key)].second;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline T& SpatialMap<T>::operator[](const cv::Point_<P>& position)
    {
        const spatial_key key = key_of(position);

        if(!contains(key))
            return emplace_at(key);
        else return at(key);
    }

    //---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline bool SpatialMap<T>::within_bounds(const cv::Point_<P>& position) const
    {
        // NOTE: The bottom and right edges of the region are exclusive.
        // That is, spatial indexing starts counting from zero just like arrays.
        const auto br = m_InputRegion.br();
        return between<int>(static_cast<int>(position.x), m_InputRegion.x, br.x - 1)
               && between<int>(static_cast<int>(position.y), m_InputRegion.y, br.y - 1);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline SpatialMap<T>::spatial_key SpatialMap<T>::key_of(const cv::Point_<P>& position) const
    {
        LVK_ASSERT(within_bounds(position));

        return simplify_key(
            position - cv::Point_<P>(m_InputRegion.x, m_InputRegion.y),
            m_KeySize
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline bool SpatialMap<T>::contains(const spatial_key key) const
    {
        LVK_ASSERT(is_key_valid(key));

        return !is_data_link_empty(fetch_data_link(key));
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const cv::Size_<float>& SpatialMap<T>::key_size() const
    {
        return m_KeySize;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const cv::Rect& SpatialMap<T>::input_region() const
    {
        return m_InputRegion;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline const cv::Size& SpatialMap<T>::resolution() const
    {
        return m_MapResolution;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline size_t SpatialMap<T>::capacity() const
    {
        return m_Map.size();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline bool SpatialMap<T>::is_empty() const
    {
        return m_Data.empty();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline size_t SpatialMap<T>::size() const
    {
        return m_Data.size();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    size_t SpatialMap<T>::rows() const
    {
        return static_cast<size_t>(m_MapResolution.height);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    size_t SpatialMap<T>::cols() const
    {
        return static_cast<size_t>(m_MapResolution.width);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline void SpatialMap<T>::clear()
    {
        m_Data.clear();

        for(auto& pointer : m_Map)
            pointer = m_EmptySymbol;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline cv::Point_<P> SpatialMap<T>::distribution_centroid() const
    {
        if(m_Data.empty())
            return {};

        cv::Point_<P> centroid;
        for(const auto& [point, value] : m_Data)
            centroid += cv::Point_<P>(point);

        centroid /= static_cast<P>(m_Data.size());

        return centroid;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline double SpatialMap<T>::distribution_quality() const
    {
        if(is_empty()) return 1.0;

        // To determine the distribution quality we will split the map into a grid of 4x4 sectors.
        // We then compare the number of items in each sector against the ideal distribution, where
        // each sector has an equal item count. We then get a percentage measure of the total number
        // of excess items in each sector, which are badly distributed, and the quality is simply
        // its inverse. If the map resolution is less than or equal to 4x4, then this technique will
        // not be meaningful so we instead approximate it by taking the map load.

        constexpr size_t sectors = 4;

        if(m_MapResolution.width <= sectors || m_MapResolution.height <= sectors)
            return static_cast<double>(m_Data.size()) / static_cast<double>(m_Map.size());

        const cv::Size distribution_resolution(sectors, sectors);
        std::array<size_t, sectors * sectors> sector_buckets{};

        const cv::Size_<float> sector_size(
            static_cast<float>(m_MapResolution.width) / static_cast<float>(sectors),
            static_cast<float>(m_MapResolution.height) / static_cast<float>(sectors)
        );

        const auto ideal_distribution = static_cast<size_t>(
            static_cast<double>(m_Data.size()) / static_cast<double>(sector_buckets.size())
        );

        double excess = 0.0;
        for(const auto& [key, data] : m_Data)
        {
            const size_t index = resolve_spatial_key(
                simplify_key(key, sector_size),
                distribution_resolution
            );

            if(++sector_buckets[index] > ideal_distribution)
                excess += 1.0;
        }

        // The maximum excess occurs when all points are in the same sector
        return  1.0 - (excess / static_cast<double>(m_Data.size() - ideal_distribution));
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::iterator SpatialMap<T>::begin()
    {
        return m_Data.begin();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::iterator SpatialMap<T>::end()
    {
        return m_Data.end();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::const_iterator SpatialMap<T>::begin() const
    {
        return m_Data.cbegin();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::const_iterator SpatialMap<T>::end() const
    {
        return m_Data.cend();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::const_iterator SpatialMap<T>::cbegin() const
    {
        return m_Data.cbegin();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline SpatialMap<T>::const_iterator SpatialMap<T>::cend() const
    {
        return m_Data.cend();
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    template<typename P>
    inline SpatialMap<T>::spatial_key SpatialMap<T>::simplify_key(
        const cv::Point_<P>& point,
        const cv::Size_<float>& key_size
    )
    {
        return spatial_key(
            static_cast<size_t>(static_cast<float>(point.x) / key_size.width),
            static_cast<size_t>(static_cast<float>(point.y) / key_size.height)
        );
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline size_t SpatialMap<T>::resolve_spatial_key(
        const spatial_key key,
        const cv::Size resolution
    )
    {
        return index_2d(key.x, key.y, resolution.width);
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline bool SpatialMap<T>::is_key_valid(const spatial_key key) const
    {
        return key.x < m_MapResolution.width && key.y < m_MapResolution.height;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline size_t& SpatialMap<T>::fetch_data_link(const spatial_key key)
    {
        return  m_Map[resolve_spatial_key(key, m_MapResolution)];
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline size_t SpatialMap<T>::fetch_data_link(const spatial_key key) const
    {
        return  m_Map[resolve_spatial_key(key, m_MapResolution)];
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline void SpatialMap<T>::clear_data_link(size_t& link)
    {
        link = m_EmptySymbol;
    }

//---------------------------------------------------------------------------------------------------------------------

    template<typename T>
    inline bool SpatialMap<T>::is_data_link_empty(const size_t link) const
    {
        return link == m_EmptySymbol;
    }

//---------------------------------------------------------------------------------------------------------------------

}
