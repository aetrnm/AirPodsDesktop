//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "TrayIcon.h"

#include <QFont>
#include <QPainter>
#include <QSvgRenderer>

#include <Config.h>
#include "../Application.h"
#include "InfoWindow.h"

namespace Gui {

TrayIcon::TrayIcon()
{
    connect(_actionSettings, &QAction::triggered, this, &TrayIcon::OnSettingsClicked);
    connect(_actionQuit, &QAction::triggered, qApp, &QApplication::quit, Qt::QueuedConnection);
    connect(_tray, &QSystemTrayIcon::activated, this, &TrayIcon::OnIconClicked);
    connect(_tray, &QSystemTrayIcon::messageClicked, this, [this]() { ShowInfoWindow(); });

    connect(
        this, &TrayIcon::OnTrayIconBatteryChangedSafety, this, &TrayIcon::OnTrayIconBatteryChanged);

    _menu->addAction(_actionSettings);
    _menu->addSeparator();
    _menu->addAction(_actionQuit);

    _tray->setContextMenu(_menu);
    _tray->setIcon(ApdApplication::windowIcon());
    _tray->show();

    if (ApdApplication::IsFirstTimeUse()) {
        _tray->showMessage(
            tr("You can find me in the system tray"),
            tr("Click the icon to view battery information, right-click to "
               "customize settings or quit."));
    }
}

void TrayIcon::UpdateState(const Core::AirPods::State &state)
{
    QString toolTip;
    std::optional<Core::AirPods::Battery::value_type> minBattery;

    // toolTip += Helper::ToString(state.model);
    toolTip += Core::AirPods::GetDisplayName();

    // clang-format off

    if (state.pods.left.battery.has_value()) {
        const auto batteryValue = state.pods.left.battery.value();

        toolTip += QString{tr("\nLeft: %1%%2")}
            .arg(batteryValue)
            .arg(state.pods.left.isCharging ? tr(" (charging)") : "");

        minBattery = batteryValue;
    }

    if (state.pods.right.battery.has_value()) {
        const auto batteryValue = state.pods.right.battery.value();

        toolTip += QString{tr("\nRight: %1%%2")}
            .arg(batteryValue)
            .arg(state.pods.right.isCharging ? tr(" (charging)") : "");

        if (minBattery.has_value() && batteryValue < minBattery.value() ||
            !minBattery.has_value()) {
            minBattery = batteryValue;
        }
    }

    if (state.caseBox.battery.has_value()) {
        toolTip += QString{tr("\nCase: %1%%2")}
            .arg(state.caseBox.battery.value())
            .arg(state.caseBox.isCharging ? tr(" (charging)") : "");
    }

    // clang-format on

    _tray->setToolTip(toolTip);

    if (minBattery.has_value() && Core::Settings::ConstAccess()->tray_icon_battery) {
        auto optIcon = GenerateIcon(64, QString::number(minBattery.value()), std::nullopt);
        if (optIcon.has_value()) {
            _tray->setIcon(QIcon{QPixmap::fromImage(optIcon.value())});
        }
    }
    else {
        _tray->setIcon(ApdApp->windowIcon());
    }
}

void TrayIcon::Unavailable()
{
    _tray->setToolTip(tr("Unavailable"));
    _tray->setIcon(ApdApp->windowIcon());
}

void TrayIcon::Disconnect()
{
    _tray->setToolTip(tr("Disconnected"));
    _tray->setIcon(ApdApp->windowIcon());
}

void TrayIcon::Unbind()
{
    _tray->setToolTip(tr("Waiting for Binding"));
    _tray->setIcon(ApdApp->windowIcon());
}

void TrayIcon::ShowInfoWindow()
{
    ApdApp->GetInfoWindow()->show();
}

std::optional<QImage> TrayIcon::GenerateIcon(
    int size, const std::optional<QString> &optText, const std::optional<QColor> &dot)
{
    QImage result{size, size, QImage::Format_ARGB32};
    QPainter painter{&result};

    result.fill(Qt::transparent);

    QSvgRenderer{QString{Config::QrcIconSvg}}.render(&painter);

    painter.setRenderHint(QPainter::Antialiasing);

    painter.save();
    do {
        if (!optText.has_value() || optText->isEmpty()) {
            break;
        }
        const auto &text = optText.value();

        static std::unordered_map<int, std::optional<QFont>> trayIconFonts;

        const auto &adjustFont = [](const QString &family,
                                    int desiredSize) -> std::optional<QFont> {
            int lastHeight = 0;

            for (int i = 1; i < 100; i++) {
                QFont font{family, i};
                font.setBold(true);

                int currentHeight = QFontMetrics{font}.height();
                if (currentHeight == desiredSize ||
                    lastHeight < desiredSize && currentHeight > desiredSize) [[unlikely]]
                {
                    SPDLOG_INFO(
                        "Found a suitable font for the tray icon. "
                        "Family: '{}', desiredSize: '{}', fontHeight: '{}', fontSize: '{}'",
                        family, desiredSize, currentHeight, i);
                    return font;
                }
                lastHeight = currentHeight;
            }

            SPDLOG_WARN(
                "Cannot find a suitable font for the tray icon. Family: '{}', desiredSize: "
                "'{}'",
                family, desiredSize);

            return std::nullopt;
        };

        auto textHeight = size * 0.8;

        if (!trayIconFonts.contains(textHeight)) {
            trayIconFonts[textHeight] = adjustFont(ApdApp->font().family(), textHeight);
        }

        const auto &optFont = trayIconFonts[textHeight];
        if (!optFont.has_value()) {
            break;
        }
        const auto &font = optFont.value();
        const auto &fontMetrics = QFontMetrics{font};

        const auto textWidth = fontMetrics.width(text);
        textHeight = fontMetrics.height();

        constexpr auto kMargin = QSizeF{2, 0};

        const auto textRect = QRectF{
            (double)size - textWidth - kMargin.width(), size - textHeight - kMargin.height(),
            (double)textWidth, textHeight};
        const auto bgRect = QRectF{
            textRect.left() - kMargin.width(), textRect.top() - kMargin.height(),
            textRect.width() + kMargin.width() * 2, textRect.height() + kMargin.height() * 2};

        painter.setPen(Qt::white);
        painter.setBrush(QColor{255, 36, 66});
        painter.setFont(font);

        painter.drawRoundedRect(bgRect, 10, 10);
        painter.drawText(textRect, text);

    } while (false);
    painter.restore();

    painter.save();
    do {
        if (!dot.has_value()) {
            break;
        }

        const double dotDiameter = size * 0.4;

        painter.setBrush(dot.value());
        painter.drawEllipse(QRectF{size - dotDiameter, 0, dotDiameter, dotDiameter});
    } while (false);
    painter.restore();

    return result;
}

void TrayIcon::OnSettingsClicked()
{
    _settingsWindow.show();
}

void TrayIcon::OnIconClicked(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::MiddleClick)
    {
        ShowInfoWindow();
    }
}

void TrayIcon::OnTrayIconBatteryChanged(bool value)
{
    auto optState = Core::AirPods::GetCurrentState();
    if (optState.has_value()) {
        UpdateState(optState.value());
    }
}

} // namespace Gui