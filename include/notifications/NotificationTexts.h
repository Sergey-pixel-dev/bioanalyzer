#ifndef NOTIFICATIONS_NOTIFICATIONTEXTS_H
#define NOTIFICATIONS_NOTIFICATIONTEXTS_H

// Каталог текстовых констант для модуля сообщений интерфейса.
// Все строки помечены для перевода через QT_TRANSLATE_NOOP: реальный
// перевод выполняется в момент вызова Notifications::tr(...), а не при
// компиляции — это позволяет менять локаль без перекомпиляции.
//
// Название группы соответствует контексту перевода: "Notifications".

#include <QCoreApplication>

namespace NotificationTexts {

// Заголовки диалогов и статусных сообщений (короткие).
inline constexpr auto TitleCaptureSettings  = QT_TRANSLATE_NOOP("Notifications", "Capture settings");
inline constexpr auto TitleDataset          = QT_TRANSLATE_NOOP("Notifications", "Dataset");
inline constexpr auto TitleModel            = QT_TRANSLATE_NOOP("Notifications", "Model");
inline constexpr auto TitleDevice           = QT_TRANSLATE_NOOP("Notifications", "Device");
inline constexpr auto TitleStream           = QT_TRANSLATE_NOOP("Notifications", "Stream");
inline constexpr auto TitleTraining         = QT_TRANSLATE_NOOP("Notifications", "Training");
inline constexpr auto TitleInference        = QT_TRANSLATE_NOOP("Notifications", "Inference");

// Тексты сообщений (частично с плейсхолдерами вида "%1").
inline constexpr auto DatasetFolderRequired   = QT_TRANSLATE_NOOP("Notifications", "Choose a dataset folder first.");
inline constexpr auto DatasetFolderCreateFail = QT_TRANSLATE_NOOP("Notifications", "Unable to create dataset folder: %1");
inline constexpr auto DatasetOpenFail         = QT_TRANSLATE_NOOP("Notifications", "Failed to open dataset: %1");
inline constexpr auto DatasetValidationFail   = QT_TRANSLATE_NOOP("Notifications", "Capture parameters are invalid: %1");
inline constexpr auto ModelLoadFail           = QT_TRANSLATE_NOOP("Notifications", "Failed to load model: %1");

inline constexpr auto DeviceScanning          = QT_TRANSLATE_NOOP("Notifications", "Scanning for devices...");
inline constexpr auto DeviceConnected         = QT_TRANSLATE_NOOP("Notifications", "Device connected: %1");
inline constexpr auto DeviceDisconnected      = QT_TRANSLATE_NOOP("Notifications", "Device disconnected.");
inline constexpr auto StreamStarted           = QT_TRANSLATE_NOOP("Notifications", "Stream started.");
inline constexpr auto StreamStopped           = QT_TRANSLATE_NOOP("Notifications", "Stream stopped.");
inline constexpr auto StatusReady             = QT_TRANSLATE_NOOP("Notifications", "Ready");

} // namespace NotificationTexts

#endif // NOTIFICATIONS_NOTIFICATIONTEXTS_H
