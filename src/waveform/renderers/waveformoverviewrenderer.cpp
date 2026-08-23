#include "waveformoverviewrenderer.h"

#include <QPainter>

#include "util/colorcomponents.h"
#include "util/math.h"
#include "util/timer.h"
#include "waveform/renderers/waveformsignalcolors.h"

namespace waveformOverviewRenderer {

QImage render(ConstWaveformPointer pWaveform,
        mixxx::OverviewType type,
        const WaveformSignalColors& signalColors,
        bool mono) {
    const int dataSize = pWaveform->getDataSize();
    if (dataSize <= 0) {
        return QImage();
    }

    QImage image(dataSize / 2, 2 * 255, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0, 0, 0, 0).value());

    QPainter painter(&image);
    painter.translate(0.0, static_cast<double>(image.height()) / 2.0);

    if (type == mixxx::OverviewType::HSV) {
        drawWaveformPartHSV(&painter,
                pWaveform,
                nullptr,
                dataSize,
                signalColors,
                mono);
    } else if (type == mixxx::OverviewType::Filtered) {
        drawWaveformPartLMH(&painter,
                pWaveform,
                nullptr,
                dataSize,
                signalColors,
                mono);
    } else {
        drawWaveformPartRGB(&painter,
                pWaveform,
                nullptr,
                dataSize,
                signalColors,
                mono);
    }

    // Evaluate waveform ratio peak
    float peak = 1;
    for (int i = 0; i < dataSize; i += 2) {
        peak = math_max3(
                peak,
                static_cast<float>(pWaveform->getAll(i)),
                static_cast<float>(pWaveform->getAll(i + 1)));
    }
    // Normalize
    float diffGain = 0;
    if (peak > 1) {
        diffGain = 255 - peak - 1;
    }

    const int topLeft = static_cast<int>(mono ? diffGain * 2 : diffGain);
    const QRect sourceRect(0,
            topLeft,
            image.width(),
            image.height() -
                    2 * static_cast<int>(diffGain));
    QImage croppedImage = image.copy(sourceRect);
    // Copy image, otherwise QPainter crashes when we alter it.
    QImage normImage = croppedImage.scaled(image.size(),
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation);

    return normImage;
}

void drawWaveformPartRGB(
        QPainter* pPainter,
        ConstWaveformPointer pWaveform,
        int* start,
        int end,
        const WaveformSignalColors& signalColors,
        bool mono) {
    ScopedTimer t(QStringLiteral("waveformOverviewRenderer::drawNextPixmapPartRGB"));
    int startVal = 0;
    if (start) {
        startVal = *start;
    }

    QColor color;

    // Same mixer as the scrolling waveform, same numbers out of the skin, so
    // the card overview and the big waveform cannot drift apart.
    const WaveformBand3& band3 = signalColors.getBand3();

    // One half-column's three band levels and its height.
    const auto bands = [&](int index,
                               float* pLow,
                               float* pMid,
                               float* pHigh,
                               float* pLength) {
        *pLow = band3.low(pWaveform->getLow(index));
        *pMid = band3.mid(pWaveform->getMid(index));
        *pHigh = band3.high(pWaveform->getHigh(index));
        *pLength = band3.heightFromBands()
                ? band3.bandHeight(*pLow, *pMid, *pHigh) * 255.0f
                : static_cast<float>(pWaveform->getAll(index));
    };
    // False for a column with nothing in it, which the caller skips.
    const auto setColor = [&](float low, float mid, float high) {
        float red = 0.f, green = 0.f, blue = 0.f;
        band3.color(low, mid, high, &red, &green, &blue);
        if (red <= 0.f && green <= 0.f && blue <= 0.f) {
            return false;
        }
        color.setRgbF(red, green, blue);
        return true;
    };

    if (mono) {
        // Mono means we're going to paint from bottom to top with l+r.
        const qreal dy = pPainter->deviceTransform().dy();
        pPainter->resetTransform();
        // shift y0 to bottom
        pPainter->translate(0, 2 * dy);
        // flip y-axis
        pPainter->scale(1, -1);
        for (int i = startVal, x = startVal / 2; i < end; i += 2, ++x) {
            // Both channels stacked into one upward column: colour from the
            // two together, height from the sum, so the 0..510 range this
            // path draws into is preserved.
            float lowL, midL, highL, lengthL;
            float lowR, midR, highR, lengthR;
            bands(i, &lowL, &midL, &highL, &lengthL);
            bands(i + 1, &lowR, &midR, &highR, &lengthR);
            const float length = lengthL + lengthR;
            if (length <= 0.f || !setColor(lowL + lowR, midL + midR, highL + highR)) {
                continue;
            }
            pPainter->setPen(color);
            pPainter->drawLine(x, static_cast<int>(length), x, 0);
        }
    } else { // stereo
        for (int i = startVal, x = startVal / 2; i < end; i += 2, ++x) {
            // One half-column per channel, each with its own colour.
            for (int chn = 0; chn < 2; ++chn) {
                float low, mid, high, length;
                bands(i + chn, &low, &mid, &high, &length);
                if (length <= 0.f || !setColor(low, mid, high)) {
                    continue;
                }
                pPainter->setPen(color);
                if (chn == 0) {
                    pPainter->drawLine(x, static_cast<int>(-length), x, 0);
                } else {
                    pPainter->drawLine(x, 0, x, static_cast<int>(length));
                }
            }
        }
    }

    if (start) {
        *start = end;
    }
}

void drawWaveformPartLMH(
        QPainter* pPainter,
        ConstWaveformPointer pWaveform,
        int* start,
        int end,
        const WaveformSignalColors& signalColors,
        bool mono) {
    ScopedTimer t(QStringLiteral("waveformOverviewRenderer::drawNextPixmapPartLMH"));
    const QColor lowColor = signalColors.getLowColor();
    const QColor midColor = signalColors.getMidColor();
    const QColor highColor = signalColors.getHighColor();
    // WOverview reads no per-band gain in stock Mixxx, so all three bands
    // are drawn at equal gain and the high band -- which covers most of the
    // lane -- buries the other two. The mixer's tables are what turn a pale
    // block back into a waveform. Colours stay the LMH pens; only the
    // levels change.
    const WaveformBand3& band3 = signalColors.getBand3();
    const auto extent = [](float level) {
        return static_cast<int>(math_clamp(level, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    int startVal = 0;
    if (start) {
        startVal = *start;
    }

    if (mono) {
        // Mono means we're going to paint from bottom to top with l+r.
        const qreal dy = pPainter->deviceTransform().dy();
        pPainter->resetTransform();
        // shift y0 to bottom
        pPainter->translate(0, 2 * dy);
        // flip y-axis
        pPainter->scale(1, -1);

        for (int i = startVal, x = startVal / 2; i < end; i += 2, ++x) {
            x = i / 2;
            pPainter->setPen(lowColor);
            pPainter->drawLine(QPoint(x, 0),
                    QPoint(x,
                            extent(band3.low(pWaveform->getLow(i))) +
                                    extent(band3.low(pWaveform->getLow(i + 1)))));

            pPainter->setPen(midColor);
            pPainter->drawLine(QPoint(x, 0),
                    QPoint(x,
                            extent(band3.mid(pWaveform->getMid(i))) +
                                    extent(band3.mid(pWaveform->getMid(i + 1)))));

            pPainter->setPen(highColor);
            pPainter->drawLine(QPoint(x, 0),
                    QPoint(x,
                            extent(band3.high(pWaveform->getHigh(i))) +
                                    extent(band3.high(pWaveform->getHigh(i + 1)))));
        }
    } else { // stereo
        for (int i = startVal, x = startVal / 2; i < end; i += 2, ++x) {
            x = i / 2;
            pPainter->setPen(lowColor);
            pPainter->drawLine(
                    QPoint(x, -extent(band3.low(pWaveform->getLow(i)))),
                    QPoint(x, extent(band3.low(pWaveform->getLow(i + 1)))));

            pPainter->setPen(midColor);
            pPainter->drawLine(
                    QPoint(x, -extent(band3.mid(pWaveform->getMid(i)))),
                    QPoint(x, extent(band3.mid(pWaveform->getMid(i + 1)))));

            pPainter->setPen(highColor);
            pPainter->drawLine(
                    QPoint(x, -extent(band3.high(pWaveform->getHigh(i)))),
                    QPoint(x, extent(band3.high(pWaveform->getHigh(i + 1)))));
        }
    }

    if (start) {
        *start = end;
    }
}

void drawWaveformPartHSV(
        QPainter* pPainter,
        ConstWaveformPointer pWaveform,
        int* start,
        int end,
        const WaveformSignalColors& signalColors,
        bool mono) {
    ScopedTimer t(QStringLiteral("waveformOverviewRenderer::drawNextPixmapPartHSV"));
    int startVal = 0;
    if (start) {
        startVal = *start;
    }

    float h = 0, s = 0, v = 0, lo = 0, hi = 0, total = 0;
    // Get HSV of low color.
    const QColor lowColor = signalColors.getLowColor();
    getHsvF(lowColor, &h, &s, &v);
    QColor color;

    unsigned char low[2] = {0, 0};
    unsigned char high[2] = {0, 0};
    unsigned char mid[2] = {0, 0};
    unsigned char all[2] = {0, 0};

    if (mono) {
        // Mono means we're going to paint from bottom to top with l+r.
        const qreal dy = pPainter->deviceTransform().dy();
        pPainter->resetTransform();
        // shift y0 to bottom
        pPainter->translate(0, 2 * dy);
        // flip y-axis
        pPainter->scale(1, -1);
    }

    for (int i = startVal, x = startVal / 2; i < end; i += 2, ++x) {
        x = i / 2;
        all[0] = pWaveform->getAll(i);
        all[1] = pWaveform->getAll(i + 1);

        if (!all[0] && !all[1]) {
            continue;
        }

        low[0] = pWaveform->getLow(i);
        low[1] = pWaveform->getLow(i + 1);
        mid[0] = pWaveform->getMid(i);
        mid[1] = pWaveform->getMid(i + 1);
        high[0] = pWaveform->getHigh(i);
        high[1] = pWaveform->getHigh(i + 1);

        total = (low[0] + low[1] + mid[0] + mid[1] +
                        high[0] + high[1]) *
                1.2f;

        // Prevent division by zero
        if (total > 0) {
            // Normalize low and high
            // (mid not need, because it not change the color)
            lo = (low[0] + low[1]) / total;
            hi = (high[0] + high[1]) / total;
        } else {
            lo = hi = 0.0;
        }

        // Set color
        color.setHsvF(h, 1.0f - hi, 1.0f - lo);

        if (mono) {
            pPainter->setPen(color);
            pPainter->drawLine(QPoint(i / 2, 0),
                    QPoint(i / 2, all[0] + all[1]));
        } else {
            pPainter->setPen(color);
            pPainter->drawLine(QPoint(i / 2, -all[0]),
                    QPoint(i / 2, all[1]));
        }
    }

    if (start) {
        *start = end;
    }
}

} // namespace waveformOverviewRenderer
