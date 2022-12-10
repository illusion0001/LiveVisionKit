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

#include "CompositeFilter.hpp"

namespace lvk
{

//---------------------------------------------------------------------------------------------------------------------

    CompositeFilter::CompositeFilter(const CompositeFilterSettings& settings)
        : VideoFilter("Composite Filter")
    {
        this->configure(settings);
    }

//---------------------------------------------------------------------------------------------------------------------

    CompositeFilter::CompositeFilter(
        const std::initializer_list<std::shared_ptr<lvk::VideoFilter>> filter_chain,
        const CompositeFilterSettings& settings
    )
        : CompositeFilter(CompositeFilterSettings{
                // TODO: add any new settings...
                .filter_chain = filter_chain
          })
    {}

//---------------------------------------------------------------------------------------------------------------------

    void CompositeFilter::process(
        const Frame& input,
        Frame& output,
        const bool debug
    )
    {
        auto& filter_chain = m_Settings.filter_chain;

        // If there are no filters, then this is simply an identity filter
        if(filter_chain.empty())
            VideoFilter::process(input, output, debug);

        const Frame* prev_filter_output = &input;
        for(size_t i = 0; i < filter_chain.size(); i++)
        {
            if(bool filter_enabled = m_FilterRunState[i]; filter_enabled)
            {
                const Frame& filter_input = *prev_filter_output;
                Frame& filter_output = m_FilterOutputs[i];

                filter_chain[i]->process(
                    filter_input,
                    filter_output,
                    debug
                );
                prev_filter_output = &filter_output;
            }
        }

        output.copy(*prev_filter_output);
    }

//---------------------------------------------------------------------------------------------------------------------

    void CompositeFilter::configure(const CompositeFilterSettings& settings)
    {
        m_Settings = settings;

        m_FilterOutputs.resize(settings.filter_chain.size());
        m_FilterRunState.resize(settings.filter_chain.size(), true);

        // Reset all filters to their enabled states
        enable_all_filters();
    }

//---------------------------------------------------------------------------------------------------------------------

    const std::vector<std::shared_ptr<lvk::VideoFilter>>& CompositeFilter::filters() const
    {
        return m_Settings.filter_chain;
    }

//---------------------------------------------------------------------------------------------------------------------

    std::shared_ptr<lvk::VideoFilter> CompositeFilter::filters(const size_t index)
    {
        LVK_ASSERT(index < m_Settings.filter_chain.size());

        return m_Settings.filter_chain[index];
    }

//---------------------------------------------------------------------------------------------------------------------

    const std::vector<Frame>& CompositeFilter::outputs() const
    {
        return m_FilterOutputs;
    }

//---------------------------------------------------------------------------------------------------------------------

    const Frame& CompositeFilter::outputs(const size_t index)
    {
        LVK_ASSERT(index < m_FilterOutputs.size());

        return m_FilterOutputs[index];
    }

//---------------------------------------------------------------------------------------------------------------------

    bool CompositeFilter::is_filter_enabled(const size_t index)
    {
        LVK_ASSERT(index < m_FilterRunState.size());

        return m_FilterRunState[index];
    }

//---------------------------------------------------------------------------------------------------------------------

    void CompositeFilter::disable_filter(const size_t index)
    {
        LVK_ASSERT(index < m_FilterRunState.size());

        m_FilterRunState[index] = false;
    }

//---------------------------------------------------------------------------------------------------------------------

    void CompositeFilter::enable_filter(const size_t index)
    {
        LVK_ASSERT(index < m_FilterRunState.size());

        m_FilterRunState[index] = true;
    }

//---------------------------------------------------------------------------------------------------------------------

    void CompositeFilter::enable_all_filters()
    {
        for(auto&& run_state : m_FilterRunState)
            run_state = true;
    }

//---------------------------------------------------------------------------------------------------------------------

}