// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scannerswidget.h"

#include <QSharedPointer>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QGraphicsDropShadowEffect>

#include <DLabel>
#include <DFrame>
#include <DIcon>
#include <DPushButton>

ScannersWidget::ScannersWidget(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void ScannersWidget::setupUI()
{
    mainLayout = new QVBoxLayout(this);

    // Title bar with refresh button
    QHBoxLayout *titleLayout = new QHBoxLayout();
    DLabel *titleLabel = new DLabel(tr("Scanner Devices"));
    QFont font = titleLabel->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 5);
    titleLabel->setFont(font);

    refreshButton = new DIconButton();
    refreshButton->setIcon(QIcon::fromTheme("refresh"));
    refreshButton->setIconSize(QSize(32, 32));
    refreshButton->setFixedSize(36, 36);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(refreshButton);
    titleLayout->addSpacing(20);
    mainLayout->addLayout(titleLayout);

    // Device list
    deviceList = new QListWidget();
    deviceList->setSpacing(10);
    mainLayout->addWidget(deviceList);

    connect(refreshButton, &DIconButton::clicked, this, [this]() {
        emit updateDeviceListRequested();
    });
}

void ScannersWidget::updateDeviceList(QSharedPointer<ScannerDevice> scanner, QSharedPointer<WebcamDevice> webcam)
{
    deviceList->clear();

    // Add scanner devices
    QStringList scannerDevices = scanner->getAvailableDevices();
    for (const QString &name : scannerDevices) {
        addDeviceItem(name, tr("Scanner"), DeviceBase::DeviceStatus::Idle, true);
    }

    // Add webcam devices
    QStringList webcamDevices = webcam->getAvailableDevices();
    for (const QString &name : webcamDevices) {
        addDeviceItem(name, tr("Webcam"), DeviceBase::DeviceStatus::Idle, false);
    }

    // Update UI based on device availability
    if (deviceList->count() == 0) {
        DLabel *emptyLabel = new DLabel(tr("No devices found"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        deviceList->addItem(new QListWidgetItem());
        deviceList->setItemWidget(deviceList->item(0), emptyLabel);
    }
}

void ScannersWidget::addDeviceItem(const QString &name, const QString &model,
                                   const DeviceBase::DeviceStatus status, bool isScanner)
{
    QListWidgetItem *item = new QListWidgetItem(deviceList);
    item->setData(Qt::UserRole, name);
    item->setData(Qt::UserRole + 1, isScanner);

    DFrame *itemWidget = new DFrame();
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(itemWidget);
    shadow->setBlurRadius(10);
    shadow->setOffset(2, 2);
    shadow->setColor(QColor(0, 0, 0, 50));
    itemWidget->setGraphicsEffect(shadow);
    itemWidget->setBackgroundRole(QPalette::Window);

    QHBoxLayout *layout = new QHBoxLayout(itemWidget);

    // Device icon
    DLabel *iconLabel = new DLabel();
    // TODO: Replace with actual device icon
    iconLabel->setFixedSize(120, 120);
    QIcon icon;
    if (isScanner) {
        icon = QIcon::fromTheme("ic_scanner");
    } else {
        icon = QIcon::fromTheme("ic_hicam");
    }
    iconLabel->setPixmap(icon.pixmap(120, 120));
    layout->addWidget(iconLabel);

    // Device info
    QVBoxLayout *infoLayout = new QVBoxLayout();
    DLabel *nameLabel = new DLabel(name);
    DLabel *modelLabel = new DLabel();
    DLabel *statusLabel = new DLabel();
    infoLayout->addWidget(nameLabel);
    infoLayout->addWidget(modelLabel);
    infoLayout->addWidget(statusLabel);
    infoLayout->setContentsMargins(10, 10, 10, 10);
    layout->addLayout(infoLayout);
    layout->addStretch();

    // Scan button
    DPushButton *scanButton = new DPushButton(tr("Scan"));
    scanButton->setProperty("deviceName", name);
    scanButton->setProperty("isScanner", isScanner);
    scanButton->setFixedWidth(150);
    scanButton->setFixedHeight(60);
    layout->addWidget(scanButton);
    layout->setContentsMargins(10, 10, 10, 10);

    connect(scanButton, &DPushButton::clicked, this, [this, name, isScanner]() {
        emit deviceSelected(name, isScanner);
    });

    // Convert status enum to display text
    QString statusText;
    switch (status) {
    case DeviceBase::DeviceStatus::Idle:
        statusText = tr("Idle");
        scanButton->setEnabled(true);
        break;
    case DeviceBase::DeviceStatus::Offline:
        statusText = tr("Offline");
        scanButton->setEnabled(false);
        break;
    }
    modelLabel->setText(tr("Model: %1").arg(model));
    statusLabel->setText(tr("Status: %1").arg(statusText));

    // itemWidget->setLayout(layout);
    item->setSizeHint(itemWidget->sizeHint());
    deviceList->setItemWidget(item, itemWidget);
}
