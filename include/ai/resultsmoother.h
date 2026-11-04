#ifndef RESULTSMOOTHER_H
#define RESULTSMOOTHER_H

#include <string>
#include <utility>
#include <vector>

// Smoothing of the recognition results for the eye. The model answers for
// every window, up to 16 times a second, and a single wrong window would
// flash a wrong gesture. The probabilities of all labels are averaged
// exponentially with time constant tau, so that a change of the step does not
// change the smoothing; the label shown changes only when another label
// leads it by `margin` (hysteresis), so two close labels do not blink.
//
// Pure C++: no Qt.
class ResultSmoother
{
public:
    using Probabilities = std::vector<std::pair<std::string, double>>;

    // tauSeconds <= 0 turns the smoothing and the hysteresis off; stepSeconds
    // is the time between windows. Keeps the averages.
    void configure(double tauSeconds, double stepSeconds, double margin = 0.1);
    void reset();
    // One window's answer; a label missing from it counts as probability 0.
    void add(const Probabilities &probabilities);

    // Averaged probabilities, the most probable first.
    Probabilities averaged() const;
    // The label shown; empty before the first answer.
    const std::string &shown() const { return m_shown; }

private:
    double m_alpha = 1.0;
    double m_margin = 0.0;
    Probabilities m_mean;
    std::string m_shown;
};

#endif // RESULTSMOOTHER_H
