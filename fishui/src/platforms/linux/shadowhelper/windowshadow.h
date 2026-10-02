#ifndef WINDOWSHADOW_H
#define WINDOWSHADOW_H

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QRect>
#include <QTimer>

class QScreen;
class QWindow;
class KWindowShadow;

/**
 * Drop shadow for a client side decorated window.
 *
 * KWin never invents a shadow for a window it does not decorate itself, so a
 * frameless window has to render the shadow and hand the tiles over through
 * org_kde_kwin_shadow. The compositor then draws them outside and behind the
 * window, which is the only way to get a shadow that is not clipped by the
 * window's own surface.
 */
class WindowShadow : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QWindow *view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QRect geometry READ geometry WRITE setGeometry NOTIFY geometryChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(qreal strength READ strength WRITE setStrength NOTIFY strengthChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit WindowShadow(QObject *parent = nullptr) noexcept;
    ~WindowShadow() override;

    void classBegin() override {}
    void componentComplete() override;

    void setView(QWindow *view);
    QWindow *view() const;

    void setRadius(qreal value);
    qreal radius() const { return m_radius; }

    qreal strength() const { return m_strength; }
    void setStrength(qreal strength);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    /**
     * Legacy CutefishOS dock API. The dock QML still binds a rectangle
     * (root.x/root.y/root.width/root.height) here; the shadow follows the
     * window's own surface, so the value no longer has any effect. Kept as a
     * no-op so the dock does not have to change.
     */
    QRect geometry() const { return m_geometry; }
    void setGeometry(const QRect &geometry);

signals:
    void viewChanged();
    void geometryChanged();
    void radiusChanged();
    void strengthChanged();
    void enabledChanged();

private:
    void scheduleUpdate();
    void update();
    void clear();
    void watchScreen();

    QPointer<QWindow> m_view;
    QPointer<QScreen> m_screen;
    qreal m_tileScale = 0;
    qreal m_tileRadius = 0;
    qreal m_tileStrength = 0;
    qreal m_radius = 10;
    qreal m_strength = 1.2;
    bool m_enabled = true;
    QRect m_geometry;
    bool m_complete = false;
    QTimer m_updateTimer;
    KWindowShadow *m_shadow = nullptr;
};

#endif
