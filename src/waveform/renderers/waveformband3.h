#pragma once

// Three-band waveform colouring.
//
// The scrolling waveform and the overview both turn one low, one mid and one
// high level into one colour, and they used to do it with two copies of the
// same arithmetic. This is that arithmetic, once, with the numbers that decide
// its character -- the base colours, the per-band gain and curve, the way the
// three colours are combined, and where the column height comes from -- read
// out of the skin rather than compiled in. The defaults reproduce what Mixxx
// has always done: additive mix, normalised to full brightness, height from
// the separately stored overall level.
//
// Band levels arrive as the bytes the analyzer stored, so gain and curve are
// 256-entry tables built at skin load. There is no pow() in the per-pixel
// loop; four decks at 60 Hz on a Pi 4 would notice.

#include <QColor>
#include <QDomNode>
#include <algorithm>
#include <array>
#include <cmath>

#include "skin/legacy/skincontext.h"
#include "util/colorcomponents.h"
#include "util/math.h"

class WaveformBand3 {
  public:
    // How the three weighted colours become one.
    enum class Mix {
        // Divide by the largest component. Hue only: every column is drawn at
        // full brightness however quiet it is, and nothing is ever white.
        Normalized,
        // Add and clip at 1. Brightness follows level, and a column with all
        // three bands strong comes out white.
        Additive,
        // Add, and divide by the largest component only when it exceeds 1.
        // Brightness follows level, hue is never bent by clipping.
        Preserve,
    };

    // Where the column height comes from.
    enum class Height {
        All,      // the separately stored overall level, as Mixxx does
        MaxBand,  // the loudest of the three bands
        SumBands, // the three added
        Rms,      // root of the sum of squares
    };

    WaveformBand3()
            : m_lowColor(Qt::red),
              m_midColor(Qt::green),
              m_highColor(Qt::blue) {
        rebuild();
    }

    // The three base colours are read and colour-corrected by
    // WaveformSignalColors, which owns this; everything else is read here.
    void setup(const QDomNode& node,
            const SkinContext& context,
            const QColor& lowColor,
            const QColor& midColor,
            const QColor& highColor) {
        m_lowColor = lowColor;
        m_midColor = midColor;
        m_highColor = highColor;

        m_gain[0] = context.selectFloat(node, QStringLiteral("Signal3BandLowGain"), 1.0f);
        m_gain[1] = context.selectFloat(node, QStringLiteral("Signal3BandMidGain"), 1.0f);
        m_gain[2] = context.selectFloat(node, QStringLiteral("Signal3BandHighGain"), 1.0f);

        m_gamma[0] = context.selectFloat(node, QStringLiteral("Signal3BandLowGamma"), 1.0f);
        m_gamma[1] = context.selectFloat(node, QStringLiteral("Signal3BandMidGamma"), 1.0f);
        m_gamma[2] = context.selectFloat(node, QStringLiteral("Signal3BandHighGamma"), 1.0f);

        const QString mix =
                context.selectString(node, QStringLiteral("Signal3BandMix")).toLower();
        if (mix == QLatin1String("additive")) {
            m_mix = Mix::Additive;
        } else if (mix == QLatin1String("preserve")) {
            m_mix = Mix::Preserve;
        } else {
            m_mix = Mix::Normalized;
        }

        m_brightnessGamma =
                context.selectFloat(node, QStringLiteral("Signal3BandBrightnessGamma"), 1.0f);
        m_floor = context.selectFloat(node, QStringLiteral("Signal3BandFloor"), 0.0f);

        const QString height =
                context.selectString(node, QStringLiteral("Signal3BandHeight")).toLower();
        if (height == QLatin1String("max")) {
            m_height = Height::MaxBand;
        } else if (height == QLatin1String("sum")) {
            m_height = Height::SumBands;
        } else if (height == QLatin1String("rms")) {
            m_height = Height::Rms;
        } else {
            m_height = Height::All;
        }
        m_heightGain = context.selectFloat(node, QStringLiteral("Signal3BandHeightGain"), 1.0f);
        m_heightGamma = context.selectFloat(node, QStringLiteral("Signal3BandHeightGamma"), 1.0f);

        rebuild();
    }

    // A stored byte becomes a level in 0..1 (or above, if the band is gained).
    float low(unsigned char value) const {
        return m_band[0][value];
    }
    float mid(unsigned char value) const {
        return m_band[1][value];
    }
    float high(unsigned char value) const {
        return m_band[2][value];
    }

    // True when the column height is to come from the three bands rather than
    // from the separately stored overall level.
    bool heightFromBands() const {
        return m_height != Height::All;
    }

    void color(float lowLevel,
            float midLevel,
            float highLevel,
            float* pR,
            float* pG,
            float* pB) const {
        float r = lowLevel * m_lowR + midLevel * m_midR + highLevel * m_highR;
        float g = lowLevel * m_lowG + midLevel * m_midG + highLevel * m_highG;
        float b = lowLevel * m_lowB + midLevel * m_midB + highLevel * m_highB;

        const float largest = std::max({r, g, b});
        if (largest <= 0.0f) {
            *pR = 0.0f;
            *pG = 0.0f;
            *pB = 0.0f;
            return;
        }

        switch (m_mix) {
        case Mix::Normalized: {
            const float f = 1.0f / largest;
            r *= f;
            g *= f;
            b *= f;
            break;
        }
        case Mix::Preserve:
            if (largest > 1.0f) {
                const float f = 1.0f / largest;
                r *= f;
                g *= f;
                b *= f;
            }
            break;
        case Mix::Additive:
            r = std::min(r, 1.0f);
            g = std::min(g, 1.0f);
            b = std::min(b, 1.0f);
            break;
        }

        if (m_shapeBrightness) {
            r = curve(m_brightness, r);
            g = curve(m_brightness, g);
            b = curve(m_brightness, b);
        }

        *pR = r;
        *pG = g;
        *pB = b;
    }

    // Column height in 0..1, for the modes that take it from the bands.
    float bandHeight(float lowLevel, float midLevel, float highLevel) const {
        float value;
        switch (m_height) {
        case Height::MaxBand:
            value = std::max({lowLevel, midLevel, highLevel});
            break;
        case Height::SumBands:
            value = lowLevel + midLevel + highLevel;
            break;
        case Height::Rms:
            value = std::sqrt(lowLevel * lowLevel + midLevel * midLevel +
                    highLevel * highLevel);
            break;
        case Height::All:
        default:
            return 0.0f;
        }
        return curve(m_heightCurve, value * m_heightGain);
    }

  private:
    static float curve(const std::array<float, 256>& table, float value) {
        const float clamped = math_clamp(value, 0.0f, 1.0f);
        return table[static_cast<int>(clamped * 255.0f + 0.5f)];
    }

    void rebuild() {
        getRgbF(m_lowColor, &m_lowR, &m_lowG, &m_lowB);
        getRgbF(m_midColor, &m_midR, &m_midG, &m_midB);
        getRgbF(m_highColor, &m_highR, &m_highG, &m_highB);

        for (int band = 0; band < 3; ++band) {
            for (int i = 0; i < 256; ++i) {
                const float x = static_cast<float>(i) / 255.0f;
                m_band[band][i] = m_gain[band] *
                        (m_gamma[band] == 1.0f ? x : std::pow(x, m_gamma[band]));
            }
        }
        for (int i = 0; i < 256; ++i) {
            const float x = static_cast<float>(i) / 255.0f;
            m_brightness[i] = m_floor +
                    (1.0f - m_floor) *
                            (m_brightnessGamma == 1.0f ? x
                                                       : std::pow(x, m_brightnessGamma));
            m_heightCurve[i] = m_heightGamma == 1.0f ? x : std::pow(x, m_heightGamma);
        }
        m_shapeBrightness = m_brightnessGamma != 1.0f || m_floor != 0.0f;
    }

    QColor m_lowColor;
    QColor m_midColor;
    QColor m_highColor;
    float m_lowR{}, m_lowG{}, m_lowB{};
    float m_midR{}, m_midG{}, m_midB{};
    float m_highR{}, m_highG{}, m_highB{};

    float m_gain[3]{1.0f, 1.0f, 1.0f};
    float m_gamma[3]{1.0f, 1.0f, 1.0f};
    Mix m_mix{Mix::Normalized};
    float m_brightnessGamma{1.0f};
    float m_floor{0.0f};
    bool m_shapeBrightness{false};
    Height m_height{Height::All};
    float m_heightGain{1.0f};
    float m_heightGamma{1.0f};

    std::array<std::array<float, 256>, 3> m_band{};
    std::array<float, 256> m_brightness{};
    std::array<float, 256> m_heightCurve{};
};
