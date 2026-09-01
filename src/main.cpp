#include "mainwindow/MainWindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace
{
    // A calm, modern dark theme. Built on Fusion (consistent across platforms,
    // unlike the host GTK style) and tuned with a cohesive palette plus a light
    // stylesheet touch on the interactive widgets.
    void applyTheme(QApplication &app)
    {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

        const QColor bg(30, 33, 39);        // window background
        const QColor base(24, 26, 31);      // input background
        const QColor surface(38, 42, 50);   // panels / alternate rows
        const QColor text(224, 226, 230);   // primary text
        const QColor subtle(150, 155, 165); // disabled / hints
        const QColor accent(0, 150, 200);   // highlight

        QPalette p;
        p.setColor(QPalette::Window, bg);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, surface);
        p.setColor(QPalette::ToolTipBase, surface);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, surface);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Disabled, QPalette::Text, subtle);
        p.setColor(QPalette::Disabled, QPalette::WindowText, subtle);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, subtle);
        app.setPalette(p);

        app.setStyleSheet(QStringLiteral(R"(
            QWidget { font-size: 10.5pt; }
            QGroupBox {
                border: 1px solid #3a3f49;
                border-radius: 8px;
                margin-top: 12px;
                padding: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
                color: #9aa0ab;
            }
            QPushButton {
                background: #2f343d;
                border: 1px solid #3a3f49;
                border-radius: 6px;
                padding: 6px 14px;
            }
            QPushButton:hover { background: #3a404d; }
            QPushButton:pressed { background: #0096c8; }
            QPushButton:checked { background: #0096c8; border-color: #0096c8; }
            QPushButton:disabled { color: #6a6f78; }
            QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
                background: #181a1f;
                border: 1px solid #3a3f49;
                border-radius: 6px;
                padding: 4px 8px;
            }
            QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus {
                border-color: #0096c8;
            }
            /* Spin box step buttons: give them a visible surface and draw the
               arrows as triangles so they don't vanish into the dark theme. */
            QSpinBox::up-button, QDoubleSpinBox::up-button {
                subcontrol-origin: border;
                subcontrol-position: top right;
                width: 18px;
                background: #2f343d;
                border-left: 1px solid #3a3f49;
                border-bottom: 1px solid #3a3f49;
                border-top-right-radius: 6px;
            }
            QSpinBox::down-button, QDoubleSpinBox::down-button {
                subcontrol-origin: border;
                subcontrol-position: bottom right;
                width: 18px;
                background: #2f343d;
                border-left: 1px solid #3a3f49;
                border-top: 1px solid #3a3f49;
                border-bottom-right-radius: 6px;
            }
            QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
            QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
                background: #3a404d;
            }
            QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
            QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
                background: #0096c8;
            }
            QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
                width: 0;
                height: 0;
                border-left: 4px solid transparent;
                border-right: 4px solid transparent;
                border-bottom: 6px solid #d6d9de;
            }
            QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
                width: 0;
                height: 0;
                border-left: 4px solid transparent;
                border-right: 4px solid transparent;
                border-top: 6px solid #d6d9de;
            }
            QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled,
            QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {
                border-bottom-color: #5a5f68;
                border-top-color: #5a5f68;
            }
            QListWidget {
                background: #181a1f;
                border: 1px solid #3a3f49;
                border-radius: 8px;
                padding: 4px;
            }
            QListWidget::item {
                border-radius: 6px;
                padding: 6px;
            }
            QListWidget::item:selected {
                background: #0096c8;
                color: white;
            }
            QTabWidget::pane {
                border: 1px solid #3a3f49;
                border-radius: 8px;
                top: -1px;
            }
            QTabBar::tab {
                background: #2a2e36;
                border: 1px solid #3a3f49;
                border-bottom: none;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
                padding: 6px 16px;
                margin-right: 2px;
            }
            QTabBar::tab:selected { background: #0096c8; color: white; }
            QTableWidget {
                background: #181a1f;
                gridline-color: #2f343d;
                border: 1px solid #3a3f49;
                border-radius: 8px;
            }
            QHeaderView::section {
                background: #2a2e36;
                border: none;
                padding: 6px;
            }
            QSlider::groove:horizontal {
                height: 6px;
                background: #2f343d;
                border-radius: 3px;
            }
            QSlider::handle:horizontal {
                background: #0096c8;
                width: 14px;
                margin: -5px 0;
                border-radius: 7px;
            }
        )"));
    }
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    applyTheme(a);
    MainWindow w;
    w.show();
    return a.exec();
}
