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

  bool resetBlacklist = false;

  CSpectrum()
      : DisplayBuff(FwData.pDisplayBuffer), Display(DisplayBuff),
        FontSmallNr(FwData.pSmallDigs), frequencyChangeStep(400_KHz), bwMul(2),
        rssiTriggerLevel(60) {
    Display.SetFont(&FontSmallNr);
  };

  void Scan() {
    u8 rssi = 0, rssiMax = 0;
    u8 iPeak = 0;
    u32 fPeak = currentFreq;

    fMeasure = GetFStart();

    RadioDriver.ToggleAFDAC(false);
    MuteAF();

    u16 scanStep = GetScanStep();
    u8 measurementsCount = GetMeasurementsCount();

    for (u8 i = 0;
         i < measurementsCount && (Fw.PollKeyboard() == 255 || resetBlacklist);
         ++i, fMeasure += scanStep) {
      if (!resetBlacklist && rssiHistory[i] == 255) {
        continue;
      }
      RadioDriver.SetFrequency(fMeasure);
      rssi = rssiHistory[i] = GetRssi();
      if (rssi > rssiMax) {
        rssiMax = rssi;
        fPeak = fMeasure;
        iPeak = i;
      }
    }
    resetBlacklist = false;
    ++peakT;

    if (rssiMax > peakRssi || peakT >= 16) {
      peakT = 0;
      peakRssi = rssiMax;
      peakF = fPeak;
      peakI = iPeak;
    }
  }

  void DrawSpectrum() {
    for (u8 x = 0; x < 128; ++x) {
      auto v = rssiHistory[x >> BWMul2XDiv()];
      if (v != 255) {
        Display.DrawHLine(Rssi2Y(v), DrawingEndY, x);
      }
    }
  }

  void DrawNums() {
    /* Display.SetCoursorXY(0, 0);
    Display.PrintFixedDigitsNumber2(fMeasure); */

    Display.SetCoursorXY(51, 0);
    Display.PrintFixedDigitsNumber2(GetBW());

    /* Display.SetCoursorXY(0, 0);
    Display.PrintFixedDigitsNumber2(rssiMinV, 0); */

    Display.SetCoursorXY(86, 0);
    Display.PrintFixedDigitsNumber2(peakF);

    Display.SetCoursorXY(44, 48);
    Display.PrintFixedDigitsNumber2(currentFreq);

    Display.SetCoursorXY(100, 48);
    Display.PrintFixedDigitsNumber2(frequencyChangeStep);

    /* Display.SetCoursorXY(0, 8);
    Display.PrintFixedDigitsNumber2(rssiMaxV, 0); */
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

    // center
    *(FwData.pDisplayBuffer + BarPos + 64) = 0b10101000;

    for (u8 i = 0; i < 128; ++i, f += step) {
      u8 barValue = 0b00001000;
      modulo(f, 100_KHz) < step && (barValue = 0b00011000);
      modulo(f, 500_KHz) < step && (barValue = 0b00111000);
      modulo(f, 1_MHz) < step && (barValue = 0b11111000);

      *(FwData.pDisplayBuffer + BarPos + i) = barValue;
    }
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

  void OnKey(u8 key) {
    switch (key) {
    case 3:
      UpdateRssiTriggerLevel(1);
      Fw.DelayMs(90);
      break;
    case 9:
      UpdateRssiTriggerLevel(-1);
      Fw.DelayMs(90);
      break;
    }
  }

  void OnKeyDown(u8 key) {
    switch (key) {
    case 2:
      UpdateBWMul(1);
      resetBlacklist = true;
      break;
    case 8:
      UpdateBWMul(-1);
      resetBlacklist = true;
      break;
    case 14:
      UpdateFreqChangeStep(100_KHz);
      break;
    case 15:
      UpdateFreqChangeStep(-100_KHz);
      break;
    case 11: // up
      UpdateCurrentFreq(frequencyChangeStep);
      resetBlacklist = true;
      break;
    case 12: // down
      UpdateCurrentFreq(-frequencyChangeStep);
      resetBlacklist = true;
      break;
    case 5:
      ToggleBacklight();
      break;
    case 0:
      Blacklist();
      break;
    }
    ResetPeak();
  }

  bool HandleUserInput() {
    btnPrev = btn;
    btn = Fw.PollKeyboard();
    if (btn == ExitKey) {
      DeInit();
      return false;
    }
    OnKey(btn);
    if (btn != 255 && btnPrev == 255) {
      OnKeyDown(btn);
    }
    return true;
  }

  void Render() {
    DisplayBuff.ClearAll();
    DrawTicks();
    DrawArrow(peakI << BWMul2XDiv());
    DrawSpectrum();
    DrawRssiTriggerLevel();
    DrawNums();
    Fw.FlushFramebufferToScreen();
  }

  void Update() {
    if (peakRssi >= rssiTriggerLevel) {
      Listen(1000);
      return;
    }
    Scan();
  }

  void UpdateRssiTriggerLevel(i32 diff) {
    rssiTriggerLevel = clamp(rssiTriggerLevel + diff, 10, 255);
  }

  void UpdateBWMul(i32 diff) {
    bwMul = clamp(bwMul + diff, 0, 4);
    frequencyChangeStep = 100_KHz << bwMul;
  }

  void UpdateCurrentFreq(i64 diff) {
    currentFreq = clamp(currentFreq + diff, 18_MHz, 1300_MHz);
  }

  void UpdateFreqChangeStep(i64 diff) {
    frequencyChangeStep = clamp(frequencyChangeStep + diff, 100_KHz, 2_MHz);
  }

  void Blacklist() { rssiHistory[peakI] = 255; }

  void Handle() {
    if (RadioDriver.IsLockedByOrgFw()) {
      return;
    }

    if (!isInitialized && IsFlashLightOn()) {
      TurnOffFlashLight();
      Init();
    }

    if (isInitialized && HandleUserInput()) {
      Update();
      Render();
    }
  }

private:
  void Init() {
    currentFreq = RadioDriver.GetFrequency();
    oldAFSettings = Fw.BK4819Read(0x47);
    oldBWSettings = Fw.BK4819Read(0x43);
    MuteAF();
    SetWideBW();
    isInitialized = true;
  }

  void DeInit() {
    DisplayBuff.ClearAll();
    Fw.FlushFramebufferToScreen();
    RadioDriver.SetFrequency(currentFreq);
    RestoreOldAFSettings();
    Fw.BK4819Write(0x43, oldBWSettings);
    isInitialized = false;
  }

  void ResetPeak() {
    peakRssi = 0;
    peakF = currentFreq;
    peakT = 0;
  }

  void SetWideBW() {
    auto Reg = Fw.BK4819Read(0x43);
    Reg &= ~(0b11 << 4);
    Fw.BK4819Write(0x43, Reg | (0b11 << 4));
  }
  void MuteAF() { Fw.BK4819Write(0x47, 0); }
  void RestoreOldAFSettings() { Fw.BK4819Write(0x47, oldAFSettings); }

  void Listen(u16 durationMs) {
    if (fMeasure != peakF) {
      fMeasure = peakF;
      RadioDriver.SetFrequency(fMeasure);
      RestoreOldAFSettings();
      RadioDriver.ToggleAFDAC(true);
    }
    for (u8 i = 0; i < 16 && Fw.PollKeyboard() == 255; ++i) {
      Fw.DelayMs(durationMs >> 4);
    }
    peakRssi = rssiHistory[peakI] = GetRssi();
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

  void ResetRSSI() {
    RadioDriver.ToggleRXDSP(false);
    RadioDriver.ToggleRXDSP(true);
  }

  u8 GetRssi() {
    ResetRSSI();

    Fw.DelayUs(1400);
    return (Fw.BK4819Read(0x67) & 0x1FF) >> 1;
  }

  bool IsFlashLightOn() { return GPIOC->DATA & GPIO_PIN_3; }
  void TurnOffFlashLight() {
    GPIOC->DATA &= ~GPIO_PIN_3;
    *FwData.p8FlashLightStatus = 3;
  }

  void ToggleBacklight() { GPIOB->DATA ^= GPIO_PIN_6; }

  u8 Rssi2Y(u8 rssi) { return DrawingEndY - clamp(rssi - 20, 0, DrawingEndY); }

  i32 clamp(i32 v, i32 min, i32 max) {
    if (v <= min)
      return min;
    if (v >= max)
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

  u8 btn = 255;
  u8 btnPrev = 255;
  u32 currentFreq;
  u16 oldAFSettings;
  u16 oldBWSettings;

  bool isInitialized = false;
};
