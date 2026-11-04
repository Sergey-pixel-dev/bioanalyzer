#include "ai/resultsmoother.h"

#include <algorithm>
#include <cmath>

void ResultSmoother::configure(double tauSeconds, double stepSeconds, double margin)
{
    if (tauSeconds <= 0.0 || stepSeconds <= 0.0)
    {
        m_alpha = 1.0;
        m_margin = 0.0;
        return;
    }
    // The weight of a new window for the given time constant: after tau
    // seconds an old answer keeps 1/e of its weight, whatever the step.
    m_alpha = 1.0 - std::exp(-stepSeconds / tauSeconds);
    m_margin = std::max(0.0, margin);
}

void ResultSmoother::reset()
{
    m_mean.clear();
    m_shown.clear();
}

void ResultSmoother::add(const Probabilities &probabilities)
{
    const bool first = m_mean.empty();
    for (const auto &item : probabilities)
        if (std::none_of(m_mean.begin(), m_mean.end(), [&](const auto &mean) { return mean.first == item.first; }))
            m_mean.emplace_back(item.first, first ? item.second : 0.0);
    if (!first)
        for (auto &mean : m_mean)
        {
            const auto it = std::find_if(probabilities.begin(), probabilities.end(),
                                         [&](const auto &item) { return item.first == mean.first; });
            const double value = it != probabilities.end() ? it->second : 0.0;
            mean.second += m_alpha * (value - mean.second);
        }
    if (m_mean.empty())
        return;

    const auto leader = std::max_element(m_mean.begin(), m_mean.end(),
                                         [](const auto &a, const auto &b) { return a.second < b.second; });
    const auto shown = std::find_if(m_mean.begin(), m_mean.end(),
                                    [&](const auto &mean) { return mean.first == m_shown; });
    if (shown == m_mean.end() || leader->second >= shown->second + m_margin)
        m_shown = leader->first;
}

ResultSmoother::Probabilities ResultSmoother::averaged() const
{
    Probabilities sorted = m_mean;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    return sorted;
}
