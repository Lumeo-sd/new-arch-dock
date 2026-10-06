/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     Reion Wong <reionwong@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QPointer>
#include <QQuickView>
#include <QTimer>
#include <QVariantAnimation>

#include "activity.h"
#include "docksettings.h"
#include "applicationmodel.h"
#include "trashmanager.h"

class PreviewController;

namespace LayerShellQt
{
class Window;
}

class MainWindow : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(QRect primaryGeometry READ primaryGeometry NOTIFY primaryGeometryChanged)
    Q_PROPERTY(int direction READ direction NOTIFY directionChanged)
    // FINAL: "visibility" also exists on QWindow as an enum, and Qt warns that
    // this member overrides a member of the base object. The name stays because
    // it is the D-Bus property name in com.cutefish.Dock.xml; FINAL says the
    // shadowing is deliberate and stops QML from overriding it.
    Q_PROPERTY(int visibility READ visibility NOTIFY visibilityChanged FINAL)
    Q_PROPERTY(int style READ style NOTIFY styleChanged)
    // Auto-hide modes shrink the panel to a thin edge strip instead of
    // unmapping it; QML fades the visuals out while dockHidden is true.
    Q_PROPERTY(bool dockHidden READ dockHidden WRITE setDockHidden NOTIFY dockHiddenChanged)

public:
    explicit MainWindow(QQuickView *parent = nullptr);
    ~MainWindow();

    // DBus interface
    void add(const QString &desktop);
    void remove(const QString &desktop);
    bool pinned(const QString &desktop);

    // Callable from QML (drag & drop of .desktop files onto the dock).
    Q_INVOKABLE bool addDesktopFile(const QString &desktop);
    Q_INVOKABLE bool addDesktopFileAt(const QString &desktop, int index);

    // Smoothly resize the (layer-shell) surface to fit the current model row
    // count. Called when a drop slot is inserted (dock grows by one cell so
    // the icons never overflow behind the trash) or removed (dock shrinks).
    Q_INVOKABLE void resizeToContent(int cellOffset);

    QRect primaryGeometry() const;
    int direction() const;

    ApplicationModel *appModel() const { return m_appModel; }

    // Visibly intended rect of the dock surface in screen coordinates.
    // (QWindow::geometry() on a layer-shell window is unsafe here: QtWayland
    // has been seen to report (0, 0, w, h) regardless of where KWin placed
    // the configured anchors+margins, so auxiliary surfaces re-derive the
    // location from the same math the scene actor used.)
    QRect dockRect() const { return windowRect(0); }

    int visibility() const;
    bool dockHidden() const { return m_dockHidden; }
    void setDockHidden(bool hidden);

    void setDirection(int direction);
    void setIconSize(int iconSize);
    void setVisibility(int visibility);

    int style() const;
    void setStyle(int style);

    Q_INVOKABLE void updateSize();

signals:
    void iconSizeChanged();
    void directionChanged();
    void primaryGeometryChanged();
    void visibilityChanged();
    void styleChanged();
    void dockHiddenChanged();

private:
    QRect windowRect(int cellOffset = 0) const;
    QRect stripRect() const;
    void resizeWindow();
    void initScreens();
    void updateLayerShell();
    void bindScreenSignals();

private slots:
    void onPrimaryScreenChanged(QScreen *screen);
    void onScreensChanged();
    void onVirtualDesktopChanged();
    void onVirtualDesktopRemoved(const QString &id);
    void onPositionChanged();
    void onIconSizeChanged();
    void onVisibilityChanged();

    void onHideTimeout();
    void onShrinkTimeout();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void resizeEvent(QResizeEvent *) override;

private:
    Activity *m_activity;
    DockSettings *m_settings;
    ApplicationModel *m_appModel;
    TrashManager *m_trashManager;

    LayerShellQt::Window *m_layerShell;
    // The output the dock currently follows; its signals are re-bound on change.
    QPointer<QScreen> m_boundScreen;

    // Hovering an app icon with several windows shows live previews of its
    // windows on this auxiliary layer-shell strip just above the dock.
    PreviewController *m_previewController = nullptr;

    // Id of the Plasma virtual desktop the panel is currently mapped on. A
    // layer-shell surface belongs to the desktop it was mapped on, so the
    // panel has to unmap and map again to follow a switch.
    QString m_desktop;

    bool m_hideBlocked;
    bool m_dockHidden;

    QTimer *m_showTimer;
    QTimer *m_hideTimer;
    // Delays the shrink to the edge strip until the QML fade-out finished, so
    // the layer surface is never resized while its content is still visible.
    QTimer *m_shrinkTimer;
    // Animates the surface geometry when a drop slot opens/closes.
    QVariantAnimation *m_resizeAnimation = nullptr;
};

#endif // MAINWINDOW_H
