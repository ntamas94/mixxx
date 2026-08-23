#pragma once


#include "wnumber.h"
#include "preferences/dialog/dlgprefdeck.h"

class ControlProxy;

class WNumberPos : public WNumber {
    Q_OBJECT

  public:
    explicit WNumberPos(const QString& group, QWidget* parent = nullptr);

    // Use a custom control for the display mode instead of the global
    // [Controls],ShowDurationRemaining. The widget then toggles only
    // between ELAPSED and REMAINING on click (two-state).
    void setDisplayModeControl(const QString& group, const QString& key);

  protected:
    void mousePressEvent(QMouseEvent* pEvent) override;

  private slots:
    void setValue(double dValue) override;
    void slotSetTimeElapsed(double);
    void slotTimeRemainingUpdated(double);
    void slotSetDisplayMode(double);
    void slotSetTimeFormat(double);

  private:

    TrackTime::DisplayMode m_displayMode;
    TrackTime::DisplayFormat m_displayFormat;

    bool m_bTwoStateCustom = false;
    double m_dOldTimeElapsed;
    ControlProxy* m_pTimeElapsed;
    ControlProxy* m_pTimeRemaining;
    ControlProxy* m_pShowTrackTimeRemaining;
    ControlProxy* m_pTimeFormat;
};
