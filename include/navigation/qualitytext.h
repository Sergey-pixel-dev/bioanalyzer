#ifndef QUALITYTEXT_H
#define QUALITYTEXT_H

#include <QCoreApplication>
#include <QString>

#include <cmath>

// Names of the quality checks as the operator reads them, and how the values
// of the checks are shown. A check without a name here is shown by its id.
inline QString qualityCheckName(const QString &id)
{
    if (id == QLatin1String("deviation"))
        return QCoreApplication::translate("Quality", "deviation");
    if (id == QLatin1String("crest"))
        return QCoreApplication::translate("Quality", "spike");
    if (id == QLatin1String("low_frequency"))
        return QCoreApplication::translate("Quality", "below 20 Hz");
    if (id == QLatin1String("saturation"))
        return QCoreApplication::translate("Quality", "saturation");
    if (id == QLatin1String("flat"))
        return QCoreApplication::translate("Quality", "flat channel");
    return id;
}

// What the check finds, for tooltips.
inline QString qualityCheckDescription(const QString &id)
{
    if (id == QLatin1String("deviation"))
        return QCoreApplication::translate("Quality", "The trial is unlike the other trials of its class: modified "
                                                      "z-score of the RMS of the steady part, the largest over the channels");
    if (id == QLatin1String("crest"))
        return QCoreApplication::translate("Quality", "A spike: the peak of the steady part is too high for its RMS "
                                                      "compared with all trials");
    if (id == QLatin1String("low_frequency"))
        return QCoreApplication::translate("Quality", "Electrode or cable motion, baseline drift of a poor contact: RMS "
                                                      "below 20 Hz compared with all trials");
    if (id == QLatin1String("saturation"))
        return QCoreApplication::translate("Quality", "Samples at the limits of the ADC range: the gain is too high or "
                                                      "an electrode came off");
    if (id == QLatin1String("flat"))
        return QCoreApplication::translate("Quality", "A channel without signal: RMS near zero");
    return QString();
}

// Column title of the check's value in the table of trials.
inline QString qualityColumnTitle(const QString &id, const QString &units)
{
    if (id == QLatin1String("deviation"))
        return QCoreApplication::translate("Quality", "Deviation,\n|z|");
    if (id == QLatin1String("crest"))
        return QCoreApplication::translate("Quality", "Crest\nfactor");
    if (id == QLatin1String("low_frequency"))
        return QCoreApplication::translate("Quality", "Below\n20 Hz, %1").arg(units);
    if (id == QLatin1String("saturation"))
        return QCoreApplication::translate("Quality", "Saturation,\n%");
    if (id == QLatin1String("flat"))
        return QCoreApplication::translate("Quality", "Least RMS,\n%1").arg(units);
    return id;
}

// A measured value as it is shown: the share at the range limits in percent.
inline double qualityDisplayValue(const QString &id, double value)
{
    return id == QLatin1String("saturation") ? value * 100.0 : value;
}

// One channel's measurement in the detailed view of a trial.
inline QString qualityMeasureText(const QString &id, double value, const QString &units)
{
    const double shown = qualityDisplayValue(id, value);
    const QString number = QString::number(shown, 'f', std::abs(shown) < 10 ? 2 : 1);
    if (id == QLatin1String("deviation"))
        return QCoreApplication::translate("Quality", "RMS %1 %2").arg(number, units);
    if (id == QLatin1String("crest"))
        return QCoreApplication::translate("Quality", "crest factor %1").arg(number);
    if (id == QLatin1String("low_frequency"))
        return QCoreApplication::translate("Quality", "below 20 Hz %1 %2").arg(number, units);
    if (id == QLatin1String("saturation"))
        return QCoreApplication::translate("Quality", "at the range limits %1%").arg(number);
    if (id == QLatin1String("flat"))
        return QCoreApplication::translate("Quality", "unfiltered RMS %1 %2").arg(number, units);
    return QStringLiteral("%1 %2").arg(id, number);
}

#endif
