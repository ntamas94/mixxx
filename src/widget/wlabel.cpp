#include "widget/wlabel.h"

#include <QPainter>

#include <QEvent>
#include <QFont>

#include "moc_wlabel.cpp"
#include "skin/legacy/skincontext.h"
#include "widget/wskincolor.h"

WLabel::WLabel(QWidget* pParent)
        : QLabel(pParent),
          WBaseWidget(this),
          m_skinText(),
          m_longText(),
          m_elideMode(Qt::ElideNone),
          m_scaleFactor(1.0),
          m_highlight(0),
          m_widthHint(0) {
}

void WLabel::setup(const QDomNode& node, const SkinContext& context) {
    m_scaleFactor = context.getScaleFactor();

    // Colors
    QPalette pal = palette(); // we have to copy out the palette to edit it since it's const (probably for threadsafety)

    QDomElement bgColor = context.selectElement(node, "BgColor");
    if (!bgColor.isNull()) {
        m_qBgColor = QColor(context.nodeToString(bgColor));
        pal.setColor(this->backgroundRole(), WSkinColor::getCorrectColor(m_qBgColor));
        setAutoFillBackground(true);
    }

    m_qFgColor = QColor(context.selectString(node, "FgColor"));
    pal.setColor(this->foregroundRole(), WSkinColor::getCorrectColor(m_qFgColor));
    setPalette(pal);

    // Font size
    QString strFontSize;
    if (context.hasNodeSelectString(node, "FontSize", &strFontSize)) {
        bool widthOk = false;
        double dFontSize = strFontSize.toDouble(&widthOk);
        if (widthOk && dFontSize >= 0) {
            QFont fonti = font();
            // We do not scale the font here, because in most cases
            // this is overridden by the style sheet font size
            fonti.setPointSizeF(dFontSize);
            setFont(fonti);
        }
    }

    // Text
    if (context.hasNodeSelectString(node, "Text", &m_skinText)) {
        setText(m_skinText);
    }

    // Alignment
    QString alignment;
    if (context.hasNodeSelectString(node, "Alignment", &alignment)) {
        alignment = alignment.toLower();
        if (alignment == "right") {
            setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        } else if (alignment == "center") {
            setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        } else if (alignment == "left") {
            setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        } else {
            qDebug() << "WLabel::setup(): Alignment =" << alignment <<
                    " unknown, use right, center or left";
        }
    }

    // Adds an ellipsis to truncated text
    QString elide;
    if (context.hasNodeSelectString(node, "Elide", &elide)) {
        elide = elide.toLower();
        if (elide == "right") {
            m_elideMode = Qt::ElideRight;
        } else if (elide == "middle") {
            m_elideMode = Qt::ElideMiddle;
        } else if (elide == "left") {
            m_elideMode = Qt::ElideLeft;
        } else if (elide == "none") {
            m_elideMode = Qt::ElideNone;
        } else if (elide == "scroll") {
            // Marquee: bounce overflowing text instead of cutting it off.
            m_scrollMode = true;
            m_elideMode = Qt::ElideNone;
            if (m_pScrollTimer == nullptr) {
                m_pScrollTimer = new QTimer(this);
                m_pScrollTimer->setInterval(40);
                connect(m_pScrollTimer, &QTimer::timeout, this, [this] {
                    if (m_scrollHold > 0) {
                        m_scrollHold--;
                        return;
                    }
                    QFontMetrics metrics(font());
                    int overflow = metrics.size(0, m_longText).width() +
                            2 * frameWidth() - width();
                    if (overflow <= 0) {
                        return;
                    }
                    m_scrollPos += m_scrollDir * 1.0;
                    if (m_scrollPos >= overflow) {
                        m_scrollPos = overflow;
                        m_scrollDir = -1;
                        m_scrollHold = 25; // ~1 s pause at each end
                    } else if (m_scrollPos <= 0) {
                        m_scrollPos = 0;
                        m_scrollDir = 1;
                        m_scrollHold = 25;
                    }
                    update();
                });
            }
        } else {
            qDebug() << "WLabel::setup(): Elide =" << elide <<
                    "unknown, use right, middle, left or none.";
        }
    }
}

QString WLabel::text() const {
    return m_longText;
}

void WLabel::setText(const QString& text) {
    m_longText = text;
    if (m_scrollMode) {
        m_scrollPos = 0;
        m_scrollDir = 1;
        m_scrollHold = 25;
        QFontMetrics metrics(font());
        bool overflow = metrics.size(0, m_longText).width() +
                2 * frameWidth() > width();
        if (overflow) {
            if (m_pScrollTimer != nullptr) {
                m_pScrollTimer->start();
            }
        } else if (m_pScrollTimer != nullptr) {
            m_pScrollTimer->stop();
        }
        // Keep QLabel's own text empty; paintEvent draws the marquee.
        QLabel::setText(QString());
        update();
        return;
    }
    if (m_elideMode != Qt::ElideNone) {
        QFontMetrics metrics(font());
        // Measure the text for the optimum label width
        // frameWidth() is the maximum of the sum of margin, border and padding
        // width of the left and the right side.
        m_widthHint = metrics.size(0, m_longText).width() + 2 * frameWidth();
        QString elidedText = metrics.elidedText(
                m_longText, m_elideMode, width() - 2 * frameWidth());
        QLabel::setText(elidedText);
    } else {
        QLabel::setText(m_longText);
    }
}

bool WLabel::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::FontChange) {
        const QFont& fonti = font();
        // Change the new font on the fly by casting away its constancy
        // using setFont() here, would results into a recursive loop
        // resetting the font to the original css values.
        // Only scale pixel size fonts, point size fonts are scaled by the OS
        if (fonti.pixelSize() > 0) {
            const_cast<QFont&>(fonti).setPixelSize(
                    static_cast<int>(fonti.pixelSize() * m_scaleFactor));
        }
        // measure text with the new font
        setText(m_longText);
    }
    return QLabel::event(pEvent);
}

void WLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    setText(m_longText);
}

void WLabel::fillDebugTooltip(QStringList* debug) {
    WBaseWidget::fillDebugTooltip(debug);
    *debug << QString("Text: \"%1\"").arg(text());
}

int WLabel::getHighlight() const {
    return m_highlight;
}

void WLabel::setHighlight(int highlight) {
    if (m_highlight == highlight) {
        return;
    }
    m_highlight = highlight;
    emit highlightChanged(m_highlight);
}

QSize WLabel::sizeHint() const {
    // make sure the sizeHint fits for the entire string.
    QSize size = QLabel::sizeHint();
    if (m_elideMode != Qt::ElideNone) {
        size.setWidth(m_widthHint);
    }
    return size;
}

void WLabel::paintEvent(QPaintEvent* pEvent) {
    if (!m_scrollMode) {
        QLabel::paintEvent(pEvent);
        return;
    }
    QPainter painter(this);
    // Follow the stylesheet color; the default pen is not guaranteed to.
    painter.setPen(m_scrollColor);
    QFontMetrics metrics(font());
    int textWidth = metrics.size(0, m_longText).width();
    QRect content = contentsRect();
    int y = content.y() + (content.height() + metrics.ascent() - metrics.descent()) / 2;
    int x = content.x();
    if (textWidth <= content.width()) {
        // Fits: honour the label alignment, no scrolling.
        if (alignment() & Qt::AlignHCenter) {
            x += (content.width() - textWidth) / 2;
        } else if (alignment() & Qt::AlignRight) {
            x += content.width() - textWidth;
        }
    } else {
        x -= static_cast<int>(m_scrollPos);
    }
    painter.setClipRect(content);
    painter.drawText(QPoint(x, y), m_longText);
}
