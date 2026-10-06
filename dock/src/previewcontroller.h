#ifndef PREVIEWCONTROLLER_H
#define PREVIEWCONTROLLER_H

#include <QObject>
#include <QTimer>

class MainWindow;
class LayerShellQtWindow;
namespace LayerShellQt { class Window; }
class QQuickView;

/**
 * Window hover previews for the dock.
 *
 * One hovered app with several windows should let the user pick the window to
 * activate (even a minimized one), like Plasma /Krema/COSMIC app lists do.
 *
 * Surface strategy (mirroring Krema): the dock shows its own layer-shell
 * surface with anchors for the dock edge.  The preview gets a second
 * layer-shell surface stretched along that same edge.  Crucially, the preview
 * surface copies the dock's own placement (same edge anchors, same
 * screen-edge margins) and is sized to extend OVER the dock by the popup
 * depth, with the popup drawn flush against the dock side.  Positioning the
 * surface purely from screen-edge margins instead leaves KWin parking it
 * above the dock's exclusive zone (~70px too high).  A mask of only the
 * popup rect (plus margins away from the dock) keeps pointer events from
 * leaking onto everything underneath; the pre-shown surface itself is mapped
 * once and only its input region changes, so showing the popup is a local
 * geometry update, not a compositor round trip.
 */
class PreviewController : public QObject
{
    Q_OBJECT

    // The QML view of the popup reads these to position/size itself.
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged)
    Q_PROPERTY(qreal contentX READ contentX NOTIFY layoutChanged)
    Q_PROPERTY(qreal contentY READ contentY NOTIFY layoutChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY layoutChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged)

public:
    explicit PreviewController(MainWindow *view);
    ~PreviewController() override;

    void initialize();

    Q_INVOKABLE void showPreview(const QString &appId, qreal iconCenterX, qreal iconCenterY);
    Q_INVOKABLE void hidePreview();
    Q_INVOKABLE void hidePreviewDelayed();
    Q_INVOKABLE void cancelHide();
    Q_INVOKABLE void setHovered(bool hovered);
    Q_INVOKABLE void setContentSize(qreal width, qreal height);
    Q_INVOKABLE void activate(int windowIndex);
    Q_INVOKABLE void closeWindow(int windowIndex);

    bool isVisible() const { return m_visible; }
    QString appId() const { return m_appId; }
    qreal contentX() const { return m_contentX; }
    qreal contentY() const { return m_contentY; }
    qreal contentWidth() const { return m_contentW; }
    qreal contentHeight() const { return m_contentH; }

signals:
    void visibleChanged(bool visible);
    void appIdChanged();
    void layoutChanged();

private:
    void layout();
    void applyInputRegion();
    void clear();

    MainWindow *m_dock = nullptr;
    QQuickView *m_view = nullptr;
    LayerShellQt::Window *m_layerShell = nullptr;

    bool m_visible = false;
    bool m_hovered = false;
    QString m_appId;
    qreal m_iconCenterX = 0;
    qreal m_iconCenterY = 0;
    qreal m_contentW = 0;
    qreal m_contentH = 0;
    qreal m_contentX = 0;
    qreal m_contentY = 0;

    QTimer m_hideTimer;
};

#endif // PREVIEWCONTROLLER_H
