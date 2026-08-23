#pragma once

#include <QLabel>
#include <QColor>
#include <QTimer>

#include "widget/wbasewidget.h"

class QDomNode;
class SkinContext;

class WLabel : public QLabel, public WBaseWidget {
    Q_OBJECT
    // Marquee text colour; stylesheet colours never reach palette(), so the
    // skin sets this explicitly: qproperty-scrollColor: #ffffff;
    Q_PROPERTY(QColor scrollColor MEMBER m_scrollColor)
  public:
    explicit WLabel(QWidget* pParent=nullptr);

    virtual void setup(const QDomNode& node, const SkinContext& context);

    QString text() const;
    void setText(const QString& text);

    // The highlight property is used to restyle the widget with CSS.
    // The declaration #MyLabel[highlight="1"] { } will define the style
    // for the highlighted state.
    // See ../wwidgetgroup.h for more info
    Q_PROPERTY(int highlight READ getHighlight WRITE setHighlight NOTIFY highlightChanged)

    int getHighlight() const;
    void setHighlight(int highlight);
    QSize sizeHint() const override;

  signals:
    void highlightChanged(int highlight);

  protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* pEvent) override;
    void resizeEvent(QResizeEvent* event) override;
    void fillDebugTooltip(QStringList* debug) override;
    QString m_skinText;
    // Foreground and background colors.
    QColor m_qFgColor;
    QColor m_qBgColor;
  private:
    QString m_longText;
    Qt::TextElideMode m_elideMode;
    // Marquee mode: bounce long text left-right instead of eliding.
    bool m_scrollMode{false};
    QTimer* m_pScrollTimer{nullptr};
    double m_scrollPos{0.0};
    QColor m_scrollColor{Qt::white};
    int m_scrollDir{1};
    int m_scrollHold{0};
    double m_scaleFactor;
    int m_highlight;
    int m_widthHint;
};
