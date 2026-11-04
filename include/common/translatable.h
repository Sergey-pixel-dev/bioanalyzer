#ifndef COMMON_TRANSLATABLE_H
#define COMMON_TRANSLATABLE_H

// Messages of the Qt-free core are marked with QT_TRANSLATE_NOOP so that
// lupdate collects them into the translation table; the core itself returns
// them in English, and the interface translates them when it shows them
// (context "Errors"). Without Qt the macro is the plain string.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

#endif // COMMON_TRANSLATABLE_H
