#include "dsp/biquad.h"

#include <cmath>

namespace dsp
{

    void Biquad::configure(Type type, double fs, double f0, double q)
    {
        reset();
        if (fs <= 0 || f0 <= 0 || q <= 0)
        {
            // Pass-through.
            m_b0 = 1;
            m_b1 = m_b2 = m_a1 = m_a2 = 0;
            return;
        }

        const double w0 = 2.0 * M_PI * f0 / fs;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * q);

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
        case Type::LowPass:
            b0 = (1 - cosw0) / 2;
            b1 = 1 - cosw0;
            b2 = (1 - cosw0) / 2;
            a0 = 1 + alpha;
            a1 = -2 * cosw0;
            a2 = 1 - alpha;
            break;
        case Type::HighPass:
            b0 = (1 + cosw0) / 2;
            b1 = -(1 + cosw0);
            b2 = (1 + cosw0) / 2;
            a0 = 1 + alpha;
            a1 = -2 * cosw0;
            a2 = 1 - alpha;
            break;
        case Type::BandPass: // constant 0 dB peak gain
            b0 = alpha;
            b1 = 0;
            b2 = -alpha;
            a0 = 1 + alpha;
            a1 = -2 * cosw0;
            a2 = 1 - alpha;
            break;
        case Type::Notch:
            b0 = 1;
            b1 = -2 * cosw0;
            b2 = 1;
            a0 = 1 + alpha;
            a1 = -2 * cosw0;
            a2 = 1 - alpha;
            break;
        }

        // Normalise by a0.
        m_b0 = b0 / a0;
        m_b1 = b1 / a0;
        m_b2 = b2 / a0;
        m_a1 = a1 / a0;
        m_a2 = a2 / a0;
    }

    double Biquad::process(double x)
    {
        // Direct Form II transposed.
        const double y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

    void Biquad::process(std::vector<double> &data)
    {
        for (double &v : data)
            v = process(v);
    }

    void Biquad::reset()
    {
        m_z1 = 0;
        m_z2 = 0;
    }

    void FilterChain::configureStandard(double fs, double hpHz, double lpHz,
                                        double notchHz, double notchQ)
    {
        clear();
        if (hpHz > 0)
        {
            Biquad hp;
            hp.configure(Biquad::Type::HighPass, fs, hpHz, 0.707);
            add(hp);
        }
        if (lpHz > 0)
        {
            Biquad lp;
            lp.configure(Biquad::Type::LowPass, fs, lpHz, 0.707);
            add(lp);
        }
        if (notchHz > 0)
        {
            Biquad notch;
            notch.configure(Biquad::Type::Notch, fs, notchHz, notchQ > 0 ? notchQ : 30.0);
            add(notch);
        }
    }

    void FilterChain::add(Biquad::Type type, double sampleRate, double frequency,
                          double q, int digitalOrder)
    {
        const int sections = std::max(1, digitalOrder);
        for (int i = 0; i < sections; ++i)
        {
            Biquad stage;
            stage.configure(type, sampleRate, frequency, q);
            add(stage);
        }
    }

    double FilterChain::process(double x)
    {
        double y = x;
        for (Biquad &stage : m_stages)
            y = stage.process(y);
        return y;
    }

    void FilterChain::process(std::vector<double> &data)
    {
        for (double &v : data)
            v = process(v);
    }

    void FilterChain::reset()
    {
        for (Biquad &stage : m_stages)
            stage.reset();
    }

} // namespace dsp
