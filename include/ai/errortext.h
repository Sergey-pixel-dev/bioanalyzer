#ifndef AI_ERRORTEXT_H
#define AI_ERRORTEXT_H

#include <QCoreApplication>
#include <QString>
#include <string>

// Translates a message of the Qt-free core (marked with QT_TRANSLATE_NOOP in
// context "Errors"). Texts of external libraries pass through unchanged.
inline QString errorText(const std::string &message)
{
    return QCoreApplication::translate("Errors", message.c_str());
}

#endif // AI_ERRORTEXT_H
