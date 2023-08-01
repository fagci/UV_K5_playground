#pragma once
#include "radio.hpp"
#include "system.hpp"
#include "types.hpp"
#include "uv_k5_display.hpp"

template <const System::TOrgFunctions &Fw, const System::TOrgData &FwData,
          Radio::CBK4819<Fw> &RadioDriver>
class CSpectrum {
public:
  static constexpr auto ExitKey = 13;
  static constexpr auto DrawingEndY = 42;
  static constexpr auto BarPos = 5 * 128;

  u8 rssiHistory[128] = {};
  u32 fMeasure;

  u8 peakT = 0;
  u8 peakRssi = 0;
  u8 peakI = 0;
  u32 peakF = 0;

  CSpectrum()
      : DisplayBuff(FwData.pDisplayBuffer), Display(DisplayBuff),
        FontSmallNr(FwData.pSmallDigs), frequencyChangeStep(400_KHz), bwMul(2),
        rssiTriggerLevel(65) {
    Display.SetFont(&FontSmallNr);
  };

  bool ListenPeak() {
    if (peakRssi < rssiTriggerLevel) {
      return false;
    }

    if (fMeasure != peakF) {
      fMeasure = peakF;
      RadioDriver.SetFrequency(fMeasure);
      RestoreOldAFSettings();
      RadioDriver.ToggleAFDAC(true);
    }

    Listen(1000);

    peakRssi = GetRssi();
    rssiHistory[peakI] = peakRssi;

    return true;
  }

  void Scan() {
    u8 rssi = 0, rssiMax = 0;
    u8 iPeak = 0;
    u32 fPeak = currentFreq;

    fMeasure = GetFStart();

    RadioDriver.ToggleAFDAC(false);
    MuteAF();

    u16 scanStep = GetScanStep();
    u8 measurementsCount = GetMeasurementsCount();

    for (u8 i = 0; i < measurementsCount; ++i, fMeasure += scanStep) {
      btn = Fw.PollKeyboard();
      if (btn != 255 && btn != 11 && btn != 12) {
        break;
      }
      RadioDriver.SetFrequency(fMeasure);
      rssi = rssiHistory[i] = GetRssi();
      if (rssi > rssiMax) {
        rssiMax = rssi;
        fPeak = fMeasure;
        iPeak = i;
      }
    }
    ++peakT;

    if (rssiMax > peakRssi || peakT >= 8) {
      peakT = 0;
      peakRssi = rssiMax;
      peakF = fPeak;
      peakI = iPeak;
    }
  }

  void DrawSpectrum() {
    for (u8 x = 0; x < 128; ++x) {
      Display.DrawHLine(Rssi2Y(rssiHistory[X2I(x)]), DrawingEndY, x);
    }
  }

  void DrawNums() {
    /* Display.SetCoursorXY(0, 0);
    Display.PrintFixedDigitsNumber2(scanDelay, 0); */

    Display.SetCoursorXY(51, 0);
    Display.PrintFixedDigitsNumber2(GetBW());

    /* Display.SetCoursorXY(107, 8);
    Display.PrintFixedDigitsNumber2(peakRssi, 0); */

    Display.SetCoursorXY(86, 0);
    Display.PrintFixedDigitsNumber2(peakF);

    Display.SetCoursorXY(44, 48);
    Display.PrintFixedDigitsNumber2(currentFreq);

    Display.SetCoursorXY(100, 48);
    Display.PrintFixedDigitsNumber2(frequencyChangeStep);

    /* Display.SetCoursorXY(0, 8);
    Display.PrintFixedDigitsNumber2(rssiTriggerLevel, 0); */
  }

  void DrawRssiTriggerLevel() {
    u8 y = Rssi2Y(rssiTriggerLevel);
    for (u8 x = 0; x < 126; x += 4) {
      Display.DrawLine(x, x + 2, y);
    }
  }

  void DrawTicks() {
    u16 step = 1562_Hz << bwMul;
    u32 f = modulo(GetFStart(), 1_MHz) + step;
    for (u8 i = 0; i < 128; ++i, f += step) {
      u8 barValue = 0b00001000;
      modulo(f, 100_KHz) < step && (barValue |= 0b00010000);
      modulo(f, 500_KHz) < step && (barValue |= 0b00100000);
      modulo(f, 1_MHz) < step && (barValue |= 0b11000000);

      *(FwData.pDisplayBuffer + BarPos + i) |= barValue;
    }

    // center
    *(FwData.pDisplayBuffer + BarPos + 64) |= 0b10101010;
  }

  void DrawArrow(u8 x) {
    for (signed i = -2; i <= 2; ++i) {
      signed v = x + i;
      if (!(v & 128)) {
        *(FwData.pDisplayBuffer + BarPos + v) |=
            (3 << (abs(i) + 4)) & 0b11110000;
      }
    }
  }

  void HandleUserInput() {
    switch (btn) {
    case 2:
      UpdateBWMul(1);
      break;
    case 8:
      UpdateBWMul(-1);
      break;
    case 3:
      UpdateRssiTriggerLevel(1);
      break;
    case 9:
      UpdateRssiTriggerLevel(-1);
      break;
    case 11: // up
      UpdateCurrentFreq(frequencyChangeStep);
      break;
    case 12: // down
      UpdateCurrentFreq(-frequencyChangeStep);
      break;
    case 14:
      UpdateFreqChangeStep(100_KHz);
      break;
    case 15:
      UpdateFreqChangeStep(-100_KHz);
      break;
    case 5:
      ToggleBacklight();
      break;
    }
  }

  void Render() {
    DisplayBuff.ClearAll();
    DrawTicks();
    DrawArrow(I2X(peakI));
    DrawSpectrum();
    DrawRssiTriggerLevel();
    DrawNums();
    Fw.FlushFramebufferToScreen();
  }

  void Update() {
    if (bDisplayCleared) {
      currentFreq = RadioDriver.GetFrequency();
      OnUserInput();
      oldAFSettings = Fw.BK4819Read(0x47);
      MuteAF();
    }
    bDisplayCleared = false;

    HandleUserInput();

    if (!ListenPeak())
      Scan();
  }

  void UpdateRssiTriggerLevel(i32 diff) {
    rssiTriggerLevel = clamp(rssiTriggerLevel + diff, 10, 255);
    OnUserInput();
  }

  void UpdateBWMul(i32 diff) {
    bwMul = clamp(bwMul + diff, 0, 4);
    frequencyChangeStep = 100_KHz << bwMul;
    OnUserInput();
  }

  void UpdateCurrentFreq(i64 diff) {
    currentFreq = clamp(currentFreq + diff, 18_MHz, 1300_MHz);
    OnUserInput();
  }

  void UpdateFreqChangeStep(i64 diff) {
    frequencyChangeStep = clamp(frequencyChangeStep + diff, 100_KHz, 2_MHz);
    OnUserInput();
  }

  void ResetPeak() {
    peakRssi = 0;
    peakF = currentFreq;
    peakT = 0;
  }

  void OnUserInput() {
    ResetPeak();
    Fw.DelayMs(90);
  }

  void Handle() {
    if (RadioDriver.IsLockedByOrgFw()) {
      return;
    }

    if (!working) {
      if (IsFlashLightOn()) {
        working = true;
        TurnOffFlashLight();
      }
      return;
    }

    btn = Fw.PollKeyboard();
    if (btn == ExitKey) {
      working = false;
      RestoreParams();
      return;
    }
    Update();
    Render();
  }

private:
  void RestoreParams() {
    if (!bDisplayCleared) {
      bDisplayCleared = true;
      DisplayBuff.ClearAll();
      Fw.FlushFramebufferToScreen();
      RadioDriver.SetFrequency(currentFreq);
      RestoreOldAFSettings();
    }
  }

  void MuteAF() { Fw.BK4819Write(0x47, 0); }
  void RestoreOldAFSettings() { Fw.BK4819Write(0x47, oldAFSettings); }

  void Listen(u16 durationMs) {
    for (u8 i = 0; i < 16 && btn == 255; ++i) {
      btn = Fw.PollKeyboard();
      Fw.DelayMs(durationMs >> 4);
    }
  }

  u16 GetScanStep() { return 25_KHz >> (2 >> bwMul); }
  u32 GetBW() { return 200_KHz << bwMul; }
  u32 GetFStart() { return currentFreq - (100_KHz << bwMul); }

  u8 BWMul2XDiv() { return clamp(4 - bwMul, 0, 2); }
  u8 GetMeasurementsCount() {
    if (bwMul == 3) {
      return 64;
    }
    if (bwMul > 3) {
      return 128;
    }
    return 32;
  }
  u8 X2I(u8 x) { return x >> BWMul2XDiv(); }
  u8 I2X(u8 x) { return x << BWMul2XDiv(); }

  void ResetRSSI() {
    RadioDriver.ToggleRXDSP(false);
    RadioDriver.ToggleRXDSP(true);
  }

  u8 GetRssi() {
    ResetRSSI();

    Fw.DelayUs(800);
    return Fw.BK4819Read(0x67);
  }

  bool IsFlashLightOn() { return GPIOC->DATA & GPIO_PIN_3; }
  void TurnOffFlashLight() {
    GPIOC->DATA &= ~GPIO_PIN_3;
    *FwData.p8FlashLightStatus = 3;
  }

  void ToggleBacklight() {
    GPIOB->DATA ^= GPIO_PIN_6;
    OnUserInput();
  }

  u8 Rssi2Y(u8 rssi) {
    return clamp(DrawingEndY - (rssi - 30), 1, DrawingEndY);
  }

  i32 clamp(i32 v, i32 min, i32 max) {
    if (v < min)
      return min;
    if (v > max)
      return max;
    return v;
  }

  u32 modulo(u32 num, u32 div) {
    while (num >= div)
      num -= div;
    return num;
  }

  TUV_K5Display DisplayBuff;
  CDisplay<const TUV_K5Display> Display;
  const TUV_K5SmallNumbers FontSmallNr;

  u32 frequencyChangeStep;
  u8 bwMul;
  u8 rssiTriggerLevel;

  u8 btn;
  u32 currentFreq;
  u16 oldAFSettings;

  bool working = false;
  bool bDisplayCleared = true;
};
