#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

// 에디터 UI 테마. Dark가 기본이다.
enum class EEditorTheme : unsigned char
{
    Dark,
    Mid,
    Light,
    Kurisu,     // 크리스 테마: 검은 바탕에 적갈색 제목 표시줄과 상태 표시줄
};

// 테마 하나의 색 표. 이름의 뜻은 아래 EditorColor 주석을 따른다.
struct FEditorPalette
{
    // 배경 계층
    ImU32 BgDeepest, BgPanel, BgRaised, BgElevated, BgElevatedAlt, BgHeader;
    ImU32 BgHover, BgAccentHover, BgSelected, BgPopup, BgOverlay;
    // 테두리
    ImU32 Border, BorderStrong;
    // 글자와 아이콘
    ImU32 TextPrimary, TextSecondary, TextMuted, TextDisabled, Ink;
    // 강조색
    ImU32 Accent, AccentHover, AccentActive, AccentBright, AccentDim, AccentText;
    // 상태와 분류
    ImU32 Error, Warning, Success, SuccessDim, CategoryBlue, CategoryPurple;
    // 배경보다 밝게(어두운 테마) 또는 어둡게(밝은 테마) 덮는 색. 알파는 쓰는 곳에서 정한다.
    ImU32 Contrast;

    // ImGui 기본 위젯 전용
    ImU32 ImText, ImTextDisabled, ImWindowBg, ImFrameBg, ImFrameBgHovered, ImFrameBgActive;
    ImU32 ImTitleBg, ImMenuBarBg, ImScrollbarBg, ImScrollbarGrab, ImScrollbarGrabHovered;
    ImU32 ImSliderGrab, ImButton, ImButtonHovered, ImHeader, ImHeaderHovered, ImHeaderActive;
    ImU32 ImTab, ImTabHovered, ImTabSelected, ImTabDimmed, ImTabDimmedSelected, ImTabDimmedSelectedOverline;
    ImU32 ImPlotLines, ImPlotHistogram, ImTableHeaderBg, ImCheckMark;
    // 하단 상태 표시줄(콘텐츠 드로어 버튼 줄) 배경
    ImU32 BgStatusBar;
    // 상태 표시줄 위 버튼(콘텐츠 드로어, 출력 로그). 기본 / 드로어가 도킹된 상태
    ImU32 BgBarButton, BgBarButtonDocked;
    // 제목 표시줄 아래 1px 경계선. 제목 표시줄 배경과 구분되는 색이어야 한다.
    ImU32 TitleBarLine;
};

namespace EditorPalettes
{
    // 딥 블랙 배경 계층 위에 적갈색 강조색 하나
    inline constexpr FEditorPalette Dark{
        IM_COL32(13, 13, 16, 255), IM_COL32(18, 18, 21, 255), IM_COL32(24, 24, 28, 255), IM_COL32(30, 30, 34, 255), IM_COL32(36, 36, 41, 255), IM_COL32(42, 42, 47, 255),
        IM_COL32(55, 58, 65, 255), IM_COL32(50, 40, 41, 255), IM_COL32(66, 36, 35, 225), IM_COL32(28, 28, 31, 255), IM_COL32(16, 16, 19, 238),
        IM_COL32(46, 44, 48, 255), IM_COL32(96, 92, 94, 255),
        IM_COL32(246, 244, 244, 255), IM_COL32(205, 203, 204, 255), IM_COL32(165, 162, 165, 255), IM_COL32(120, 120, 124, 255), IM_COL32(20, 20, 22, 255),
        IM_COL32(158, 51, 43, 255), IM_COL32(191, 69, 59, 255), IM_COL32(115, 33, 28, 255), IM_COL32(215, 86, 74, 255), IM_COL32(96, 36, 32, 255), IM_COL32(235, 128, 86, 255),
        IM_COL32(242, 97, 87, 255), IM_COL32(236, 186, 74, 255), IM_COL32(112, 196, 124, 255), IM_COL32(30, 84, 46, 255), IM_COL32(90, 190, 230, 255), IM_COL32(170, 120, 220, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 255, 255, 255), IM_COL32(128, 128, 128, 255), IM_COL32(17, 17, 18, 255), IM_COL32(26, 26, 28, 255), IM_COL32(37, 37, 40, 255), IM_COL32(51, 45, 45, 255),
        IM_COL32(13, 13, 14, 255), IM_COL32(19, 19, 21, 255), IM_COL32(14, 14, 15, 255), IM_COL32(51, 51, 54, 255), IM_COL32(77, 71, 73, 255),
        IM_COL32(100, 100, 100, 255), IM_COL32(36, 36, 38, 255), IM_COL32(55, 52, 54, 255), IM_COL32(37, 37, 40, 255), IM_COL32(56, 47, 47, 255), IM_COL32(77, 43, 42, 255),
        IM_COL32(14, 14, 15, 255), IM_COL32(47, 41, 42, 255), IM_COL32(33, 33, 36, 255), IM_COL32(13, 13, 14, 255), IM_COL32(27, 27, 29, 255), IM_COL32(107, 36, 31, 255),
        IM_COL32(120, 120, 120, 255), IM_COL32(149, 149, 149, 255), IM_COL32(29, 29, 32, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(18, 18, 21, 255), IM_COL32(30, 30, 34, 255), IM_COL32(42, 42, 47, 255), IM_COL32(96, 36, 32, 255),
    };

    // 회색 바탕에 진한 회색 선
    inline constexpr FEditorPalette Mid{
        IM_COL32(52, 52, 55, 255), IM_COL32(62, 62, 65, 255), IM_COL32(70, 70, 74, 255), IM_COL32(78, 78, 82, 255), IM_COL32(85, 85, 90, 255), IM_COL32(92, 92, 97, 255),
        IM_COL32(104, 106, 112, 255), IM_COL32(98, 80, 80, 255), IM_COL32(118, 62, 58, 230), IM_COL32(74, 74, 78, 255), IM_COL32(60, 60, 64, 238),
        IM_COL32(38, 38, 41, 255), IM_COL32(24, 24, 26, 255),
        IM_COL32(244, 243, 243, 255), IM_COL32(218, 216, 217, 255), IM_COL32(184, 182, 184, 255), IM_COL32(138, 138, 142, 255), IM_COL32(40, 40, 43, 255),
        IM_COL32(168, 56, 47, 255), IM_COL32(196, 74, 63, 255), IM_COL32(125, 38, 32, 255), IM_COL32(225, 98, 84, 255), IM_COL32(112, 48, 42, 255), IM_COL32(242, 150, 110, 255),
        IM_COL32(250, 118, 108, 255), IM_COL32(242, 198, 92, 255), IM_COL32(132, 210, 142, 255), IM_COL32(50, 102, 64, 255), IM_COL32(118, 204, 238, 255), IM_COL32(190, 150, 232, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(244, 243, 243, 255), IM_COL32(150, 150, 152, 255), IM_COL32(62, 62, 65, 255), IM_COL32(48, 48, 51, 255), IM_COL32(58, 58, 62, 255), IM_COL32(88, 72, 72, 255),
        IM_COL32(46, 46, 49, 255), IM_COL32(56, 56, 59, 255), IM_COL32(54, 54, 57, 255), IM_COL32(100, 100, 104, 255), IM_COL32(128, 120, 122, 255),
        IM_COL32(150, 150, 152, 255), IM_COL32(84, 84, 88, 255), IM_COL32(104, 100, 102, 255), IM_COL32(82, 82, 86, 255), IM_COL32(102, 88, 88, 255), IM_COL32(124, 76, 72, 255),
        IM_COL32(50, 50, 53, 255), IM_COL32(96, 84, 85, 255), IM_COL32(74, 74, 78, 255), IM_COL32(46, 46, 49, 255), IM_COL32(66, 66, 70, 255), IM_COL32(128, 52, 45, 255),
        IM_COL32(170, 170, 172, 255), IM_COL32(192, 192, 194, 255), IM_COL32(72, 72, 76, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(62, 62, 65, 255), IM_COL32(78, 78, 82, 255), IM_COL32(92, 92, 97, 255), IM_COL32(112, 48, 42, 255),
    };

    // 흰색 바탕에 회색 선
    inline constexpr FEditorPalette Light{
        IM_COL32(232, 232, 235, 255), IM_COL32(246, 246, 248, 255), IM_COL32(238, 238, 241, 255), IM_COL32(252, 252, 253, 255), IM_COL32(242, 242, 245, 255), IM_COL32(226, 226, 230, 255),
        IM_COL32(220, 222, 228, 255), IM_COL32(246, 226, 224, 255), IM_COL32(230, 180, 172, 255), IM_COL32(252, 252, 253, 255), IM_COL32(246, 246, 248, 238),
        IM_COL32(204, 204, 210, 255), IM_COL32(150, 150, 158, 255),
        IM_COL32(26, 26, 30, 255), IM_COL32(64, 64, 70, 255), IM_COL32(104, 104, 112, 255), IM_COL32(156, 156, 162, 255), IM_COL32(214, 214, 220, 255),
        IM_COL32(168, 52, 42, 255), IM_COL32(196, 70, 58, 255), IM_COL32(128, 36, 28, 255), IM_COL32(190, 64, 52, 255), IM_COL32(226, 186, 180, 255), IM_COL32(160, 54, 30, 255),
        IM_COL32(196, 44, 36, 255), IM_COL32(170, 112, 0, 255), IM_COL32(34, 132, 56, 255), IM_COL32(204, 234, 210, 255), IM_COL32(16, 112, 168, 255), IM_COL32(116, 66, 176, 255),
        IM_COL32(0, 0, 0, 255),
        IM_COL32(26, 26, 30, 255), IM_COL32(150, 150, 158, 255), IM_COL32(246, 246, 248, 255), IM_COL32(255, 255, 255, 255), IM_COL32(236, 236, 240, 255), IM_COL32(244, 224, 222, 255),
        IM_COL32(226, 226, 230, 255), IM_COL32(238, 238, 241, 255), IM_COL32(236, 236, 240, 255), IM_COL32(196, 196, 202, 255), IM_COL32(168, 160, 162, 255),
        IM_COL32(150, 150, 158, 255), IM_COL32(232, 232, 236, 255), IM_COL32(220, 214, 216, 255), IM_COL32(208, 210, 218, 255), IM_COL32(226, 212, 210, 255), IM_COL32(224, 172, 164, 255),
        IM_COL32(226, 226, 230, 255), IM_COL32(240, 226, 225, 255), IM_COL32(252, 252, 253, 255), IM_COL32(222, 222, 226, 255), IM_COL32(240, 240, 243, 255), IM_COL32(206, 140, 132, 255),
        IM_COL32(120, 120, 128, 255), IM_COL32(96, 96, 104, 255), IM_COL32(236, 236, 240, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(246, 246, 248, 255), IM_COL32(252, 252, 253, 255), IM_COL32(226, 226, 230, 255), IM_COL32(226, 186, 180, 255),
    };

    // 크리스 테마. #111114 검은 바탕, #B11226 적색 강조, #5A0B16 적갈색 제목·상태 표시줄, #8E8E93 회색 보조 글자, #F2F2F4 글자.
    // 배경 계층은 아주 약하게 붉은 기를 섞어 강조색과 어울리게 한다.
    inline constexpr FEditorPalette Kurisu{
        IM_COL32(11, 11, 13, 255), IM_COL32(17, 17, 20, 255), IM_COL32(24, 22, 26, 255), IM_COL32(31, 28, 32, 255), IM_COL32(38, 34, 38, 255), IM_COL32(46, 38, 42, 255),
        IM_COL32(60, 46, 50, 255), IM_COL32(72, 26, 34, 255), IM_COL32(90, 11, 22, 235), IM_COL32(26, 22, 26, 255), IM_COL32(20, 14, 17, 238),
        IM_COL32(56, 30, 36, 255), IM_COL32(120, 64, 72, 255),
        IM_COL32(242, 242, 244, 255), IM_COL32(212, 208, 212, 255), IM_COL32(142, 142, 147, 255), IM_COL32(100, 98, 104, 255), IM_COL32(14, 12, 14, 255),
        IM_COL32(177, 18, 38, 255), IM_COL32(204, 38, 58, 255), IM_COL32(122, 12, 28, 255), IM_COL32(226, 60, 78, 255), IM_COL32(90, 11, 22, 255), IM_COL32(242, 102, 114, 255),
        IM_COL32(255, 96, 96, 255), IM_COL32(240, 190, 82, 255), IM_COL32(112, 200, 132, 255), IM_COL32(28, 80, 44, 255), IM_COL32(98, 180, 230, 255), IM_COL32(182, 132, 226, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(242, 242, 244, 255), IM_COL32(126, 124, 130, 255), IM_COL32(17, 17, 20, 255), IM_COL32(26, 22, 26, 255), IM_COL32(40, 30, 34, 255), IM_COL32(72, 24, 32, 255),
        IM_COL32(90, 11, 22, 255), IM_COL32(22, 18, 21, 255), IM_COL32(13, 12, 14, 255), IM_COL32(62, 44, 48, 255), IM_COL32(112, 40, 50, 255),
        IM_COL32(142, 142, 147, 255), IM_COL32(36, 30, 34, 255), IM_COL32(88, 28, 38, 255), IM_COL32(44, 34, 38, 255), IM_COL32(92, 30, 40, 255), IM_COL32(122, 20, 36, 255),
        IM_COL32(34, 24, 28, 255), IM_COL32(72, 30, 40, 255), IM_COL32(44, 34, 38, 255), IM_COL32(30, 22, 26, 255), IM_COL32(40, 30, 34, 255), IM_COL32(150, 24, 42, 255),
        IM_COL32(142, 142, 147, 255), IM_COL32(177, 18, 38, 255), IM_COL32(30, 24, 28, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(90, 11, 22, 255), IM_COL32(44, 34, 38, 255), IM_COL32(60, 44, 50, 255), IM_COL32(160, 28, 46, 255),
    };

    inline constexpr const FEditorPalette& Get(EEditorTheme Theme)
    {
        switch (Theme)
        {
        case EEditorTheme::Mid: return Mid;
        case EEditorTheme::Light: return Light;
        case EEditorTheme::Kurisu: return Kurisu;
        default: return Dark;
        }
    }
}

// 에디터 UI 색 팔레트. 패널에서 색을 직접 적지 말고 여기 이름을 쓴다.
// 값은 현재 테마의 색 표를 가리킨다. 테마를 바꾸면(EditorStyle::SetTheme) 다음 프레임부터 바뀐 색으로 그려진다.
// 그래서 이 값을 constexpr 상수나 static 변수에 복사해 두지 말고 쓰는 자리에서 읽는다.
namespace EditorColor
{
    inline FEditorPalette GPalette = EditorPalettes::Dark;
    inline EEditorTheme GTheme = EEditorTheme::Dark;

    // 배경. 어두운 테마는 아래로 갈수록 밝고, 밝은 테마는 패널과 카드가 가장 밝다.
    inline const ImU32& BgDeepest = GPalette.BgDeepest;          // 창 바닥, 로그·목록 안쪽
    inline const ImU32& BgPanel = GPalette.BgPanel;              // 패널, 사이드바
    inline const ImU32& BgRaised = GPalette.BgRaised;            // 패널 위에 얹는 띠, 교차 행, 분할선
    inline const ImU32& BgElevated = GPalette.BgElevated;        // 카드, 버튼, 타임라인 행
    inline const ImU32& BgElevatedAlt = GPalette.BgElevatedAlt;  // BgElevated의 교차 행
    inline const ImU32& BgHeader = GPalette.BgHeader;            // 눈금자, 빈 뷰포트
    inline const ImU32& BgHover = GPalette.BgHover;              // 카드·칸에 마우스를 올렸을 때
    inline const ImU32& BgAccentHover = GPalette.BgAccentHover;  // 마우스를 올렸을 때의 붉은 기 도는 배경
    inline const ImU32& BgSelected = GPalette.BgSelected;        // 선택된 행
    inline const ImU32& BgPopup = GPalette.BgPopup;              // 팝업, 떠 있는 창
    inline const ImU32& BgOverlay = GPalette.BgOverlay;          // 뷰포트 위에 뜨는 툴바
    inline constexpr ImU32 Scrim = IM_COL32(0, 0, 0, 140);       // 뷰포트 위 통계 글자 뒤 어둠. 테마와 상관없다.
    inline constexpr ImU32 Transparent = IM_COL32(0, 0, 0, 0);

    // 테두리
    inline const ImU32& Border = GPalette.Border;
    inline const ImU32& BorderStrong = GPalette.BorderStrong;

    // 글자와 아이콘
    inline const ImU32& TextPrimary = GPalette.TextPrimary;
    inline const ImU32& TextSecondary = GPalette.TextSecondary;
    inline const ImU32& TextMuted = GPalette.TextMuted;
    inline const ImU32& TextDisabled = GPalette.TextDisabled;
    inline const ImU32& Ink = GPalette.Ink;                      // 체크 박스 안쪽처럼 글자와 반대로 가라앉은 면

    // 강조색 (마키세 크리스 적갈색)
    inline const ImU32& Accent = GPalette.Accent;
    inline const ImU32& AccentHover = GPalette.AccentHover;
    inline const ImU32& AccentActive = GPalette.AccentActive;
    inline const ImU32& AccentBright = GPalette.AccentBright;    // 선택 테두리, 재생 헤드처럼 가는 선
    inline const ImU32& AccentDim = GPalette.AccentDim;          // 구분선, 약한 테두리
    inline const ImU32& AccentText = GPalette.AccentText;        // 배경 위 강조 글자

    // 상태
    inline const ImU32& Error = GPalette.Error;
    inline const ImU32& Warning = GPalette.Warning;
    inline const ImU32& Success = GPalette.Success;
    inline const ImU32& SuccessDim = GPalette.SuccessDim;

    // 종류를 구분하는 색. 강조색과 헷갈리지 않게 다른 색상을 쓴다.
    inline const ImU32& CategoryBlue = GPalette.CategoryBlue;
    inline const ImU32& CategoryPurple = GPalette.CategoryPurple;

    // 제목 표시줄 닫기 버튼. Windows 기본 색을 따른다.
    inline constexpr ImU32 CloseHover = IM_COL32(232, 17, 35, 255);
    inline constexpr ImU32 CloseHeld = IM_COL32(241, 112, 122, 255);

    // 뷰포트(3D 화면) 위에 뜨는 글자. 테마와 상관없이 어두운 Scrim 위에 쓴다.
    inline constexpr ImU32 OverlayText = IM_COL32(246, 244, 244, 255);
    inline constexpr ImU32 OverlayTextAccent = IM_COL32(235, 128, 86, 255);

    // 팔레트 색의 불투명도만 바꾼다.
    constexpr ImU32 WithAlpha(ImU32 Color, int Alpha)
    {
        return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
    }
    // 반투명 흰색. 이미지 틴트처럼 테마와 상관없이 흰색이어야 하는 곳에 쓴다.
    constexpr ImU32 White(int Alpha) { return IM_COL32(255, 255, 255, Alpha); }
    // 배경 위에 덮는 하이라이트와 격자선. 어두운 테마는 흰색, 밝은 테마는 검은색이다.
    inline ImU32 Contrast(int Alpha) { return WithAlpha(GPalette.Contrast, Alpha); }
    // ImVec4를 받는 API(TextColored 등)용 변환.
    inline ImVec4 ToVec4(ImU32 Color) { return ImGui::ColorConvertU32ToFloat4(Color); }
}

// 공통 스타일 함수와 레이아웃 단위. 크기는 96 DPI를 기준으로 정의한다.
namespace EditorStyle
{
    inline float Scale(float Value) { return Value * ImGui::GetStyle().FontScaleDpi; }

    struct FPopupAnchor
    {
        ImVec2 Min;
        ImVec2 Max;
    };

    inline FPopupAnchor LastItemAnchor()
    {
        return { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
    }

    // 버튼 아래에 붙이고, 아래 공간이 부족하면 위로 연다. 긴 목록은 스크롤한다.
    inline bool BeginButtonPopup(const char* Id, const FPopupAnchor& Anchor, float Width = 0.0f)
    {
        if (!ImGui::IsPopupOpen(Id)) return false;

        const ImGuiViewport* Viewport = ImGui::GetWindowViewport();
        ImVec2 WorkMin = Viewport->WorkPos;
        ImVec2 WorkMax(WorkMin.x + Viewport->WorkSize.x, WorkMin.y + Viewport->WorkSize.y);
        const ImVec2 Center((Anchor.Min.x + Anchor.Max.x) * 0.5f, (Anchor.Min.y + Anchor.Max.y) * 0.5f);
        for (const ImGuiPlatformMonitor& Monitor : ImGui::GetPlatformIO().Monitors)
        {
            if (Center.x >= Monitor.MainPos.x && Center.y >= Monitor.MainPos.y &&
                Center.x < Monitor.MainPos.x + Monitor.MainSize.x && Center.y < Monitor.MainPos.y + Monitor.MainSize.y)
            {
                WorkMin = Monitor.WorkPos;
                WorkMax = ImVec2(WorkMin.x + Monitor.WorkSize.x, WorkMin.y + Monitor.WorkSize.y);
                break;
            }
        }

        const float Gap = Scale(4.0f);
        const float Below = ImMax(1.0f, WorkMax.y - Anchor.Max.y - Gap);
        const float Above = ImMax(1.0f, Anchor.Min.y - WorkMin.y - Gap);
        const bool bAbove = Below < ImMin(Scale(320.0f), Above);
        const float MaxWidth = ImMax(1.0f, WorkMax.x - WorkMin.x);
        const float MinWidth = ImMin(MaxWidth, ImMax(Anchor.Max.x - Anchor.Min.x, Width));
        const float MaxHeight = ImMin(Scale(480.0f), bAbove ? Above : Below);
        const ImGuiContext& Context = *ImGui::GetCurrentContext();
        const ImGuiWindow* PopupWindow = Context.OpenPopupStack[Context.BeginPopupStack.Size].Window;
        const float PopupWidth = ImClamp(PopupWindow ? PopupWindow->SizeFull.x : MinWidth, MinWidth, MaxWidth);
        ImGui::SetNextWindowSizeConstraints(ImVec2(MinWidth, 0.0f), ImVec2(MaxWidth, MaxHeight));
        ImGui::SetNextWindowPos(ImVec2(ImClamp(Anchor.Min.x, WorkMin.x, WorkMax.x - PopupWidth),
            bAbove ? Anchor.Min.y - Gap : Anchor.Max.y + Gap), ImGuiCond_Always, ImVec2(0.0f, bAbove ? 1.0f : 0.0f));
        return ImGui::BeginPopup(Id, ImGuiWindowFlags_NoMove);
    }

    inline void PushSelectedButton(bool bSelected)
    {
        if (!bSelected) return;
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_CheckboxSelectedBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorColor::AccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, EditorColor::AccentActive);
    }

    inline void PopSelectedButton(bool bSelected) { if (bSelected) ImGui::PopStyleColor(3); }

    // 아웃라이너처럼 패널 맨 위에 두는 꽉 찬 폭의 검색창. 내용이 바뀌면 true.
    inline bool SearchBox(const char* Id, char* Buffer, size_t BufferSize)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::InputTextWithHint(Id, "Search...", Buffer, BufferSize);
    }

    // 대소문자를 무시한 부분 문자열 검색. 빈 패턴은 항상 일치한다.
    inline bool ContainsIgnoreCase(const char* Text, const char* Pattern)
    {
        if (!Pattern || !*Pattern) return true;
        if (!Text) return false;
        for (; *Text; ++Text)
        {
            const char* T = Text;
            const char* P = Pattern;
            while (*T && *P && ImToUpper(*T) == ImToUpper(*P)) { ++T; ++P; }
            if (!*P) return true;
        }
        return false;
    }

    inline void SameLineOrWrap(float NextWidth)
    {
        const float Right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + NextWidth <= Right)
            ImGui::SameLine();
    }

    // ImGui 기본 위젯 색을 현재 테마로 채운다. 크기·간격은 건드리지 않는다.
    inline void ApplyColors(ImGuiStyle& style)
    {
        using namespace EditorColor;
        const FEditorPalette& P = GPalette;
        ImVec4* colors = style.Colors;
        auto V = [](ImU32 Color) { return ToVec4(Color); };
        auto VA = [](ImU32 Color, float Alpha) { ImVec4 C = ToVec4(Color); C.w = Alpha; return C; };

        const ImVec4 AccentV = V(P.Accent);
        const ImVec4 AccentHoverV = V(P.AccentHover);
        const ImVec4 AccentActiveV = V(P.AccentActive);

        // Text
        colors[ImGuiCol_Text] = V(P.ImText);
        colors[ImGuiCol_TextDisabled] = V(P.ImTextDisabled);
        colors[ImGuiCol_TextLink] = AccentHoverV;
        colors[ImGuiCol_TextSelectedBg] = VA(P.Contrast, 0.156f);

        // Window Background
        colors[ImGuiCol_WindowBg] = V(P.ImWindowBg);
        colors[ImGuiCol_ChildBg] = VA(P.ImWindowBg, 0.0f);
        colors[ImGuiCol_PopupBg] = V(P.BgPopup);
        colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];

        // Borders
        colors[ImGuiCol_Border] = V(P.Border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Input Frames
        colors[ImGuiCol_FrameBg] = V(P.ImFrameBg);
        colors[ImGuiCol_FrameBgHovered] = V(P.ImFrameBgHovered);
        colors[ImGuiCol_FrameBgActive] = V(P.ImFrameBgActive);
        colors[ImGuiCol_InputTextCursor] = V(P.ImText);

        // Title & Menu
        colors[ImGuiCol_TitleBg] = V(P.ImTitleBg);
        colors[ImGuiCol_TitleBgActive] = V(P.ImTitleBg);
        colors[ImGuiCol_TitleBgCollapsed] = V(P.ImTitleBg);
        colors[ImGuiCol_MenuBarBg] = V(P.ImMenuBarBg);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = V(P.ImScrollbarBg);
        colors[ImGuiCol_ScrollbarGrab] = V(P.ImScrollbarGrab);
        colors[ImGuiCol_ScrollbarGrabHovered] = V(P.ImScrollbarGrabHovered);
        colors[ImGuiCol_ScrollbarGrabActive] = AccentActiveV;

        // Checkbox: 선택된 박스는 적갈색, 체크 표시는 흰색
        colors[ImGuiCol_CheckboxSelectedBg] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.85f);
        colors[ImGuiCol_CheckMark] = V(P.ImCheckMark);

        // Slider
        colors[ImGuiCol_SliderGrab] = V(P.ImSliderGrab);
        colors[ImGuiCol_SliderGrabActive] = AccentHoverV;

        // Button
        colors[ImGuiCol_Button] = V(P.ImButton);
        colors[ImGuiCol_ButtonHovered] = V(P.ImButtonHovered);
        colors[ImGuiCol_ButtonActive] = AccentActiveV;

        // Header, TreeNode, Selectable
        colors[ImGuiCol_Header] = V(P.ImHeader);
        colors[ImGuiCol_HeaderHovered] = V(P.ImHeaderHovered);
        colors[ImGuiCol_HeaderActive] = V(P.ImHeaderActive);

        // Separator
        colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered] = V(P.ImSliderGrab);
        colors[ImGuiCol_SeparatorActive] = AccentV;

        // Resize Grip
        colors[ImGuiCol_ResizeGrip] = VA(P.Contrast, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = VA(P.Contrast, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = AccentV;

        // Tabs
        colors[ImGuiCol_Tab] = V(P.ImTab);
        colors[ImGuiCol_TabHovered] = V(P.ImTabHovered);
        colors[ImGuiCol_TabSelected] = V(P.ImTabSelected);
        colors[ImGuiCol_TabDimmed] = V(P.ImTabDimmed);
        colors[ImGuiCol_TabDimmedSelected] = V(P.ImTabDimmedSelected);
        colors[ImGuiCol_TabSelectedOverline] = AccentV;
        colors[ImGuiCol_TabDimmedSelectedOverline] = V(P.ImTabDimmedSelectedOverline);

        // Docking
        colors[ImGuiCol_DockingPreview] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.65f);

        // Graphs
        colors[ImGuiCol_PlotLines] = V(P.ImPlotLines);
        colors[ImGuiCol_PlotLinesHovered] = AccentHoverV;
        colors[ImGuiCol_PlotHistogram] = V(P.ImPlotHistogram);
        colors[ImGuiCol_PlotHistogramHovered] = AccentHoverV;

        // Tables
        colors[ImGuiCol_TableHeaderBg] = V(P.ImTableHeaderBg);
        colors[ImGuiCol_TableBorderStrong] = colors[ImGuiCol_Border];
        colors[ImGuiCol_TableBorderLight] = V(P.ImHeader);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = VA(P.Contrast, 0.03f);

        // Misc
        colors[ImGuiCol_TreeLines] = colors[ImGuiCol_Border];
        colors[ImGuiCol_DragDropTarget] = AccentHoverV;
        colors[ImGuiCol_DragDropTargetBg] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.18f);
        colors[ImGuiCol_UnsavedMarker] = AccentV;
        colors[ImGuiCol_NavCursor] = AccentV;
        colors[ImGuiCol_NavWindowingHighlight] = AccentV;
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
    }

    inline EEditorTheme GetTheme() { return EditorColor::GTheme; }

    // 메뉴와 editor.ini가 쓰는 전체 테마 목록
    inline constexpr EEditorTheme AllThemes[] = { EEditorTheme::Dark, EEditorTheme::Mid, EEditorTheme::Light, EEditorTheme::Kurisu };

    // 팔레트와 ImGui 색을 바꾼다. 크기·간격(DPI 배율 포함)은 그대로 둔다.
    inline void SetTheme(EEditorTheme Theme)
    {
        EditorColor::GTheme = Theme;
        EditorColor::GPalette = EditorPalettes::Get(Theme);
        if (ImGui::GetCurrentContext())
            ApplyColors(ImGui::GetStyle());
    }

    inline const char* GetThemeName(EEditorTheme Theme)
    {
        switch (Theme)
        {
        case EEditorTheme::Mid: return "Mid";
        case EEditorTheme::Light: return "Light";
        case EEditorTheme::Kurisu: return "Kurisu";
        default: return "Dark";
        }
    }

    // 스타일 전체(색 + 크기·간격)를 기본값으로 다시 만든다. 시작할 때 부른다.
    inline void Apply()
    {
        ImGui::GetStyle() = ImGuiStyle();
        ImGuiStyle& style = ImGui::GetStyle();
        ApplyColors(style);

        // ------------------------------------------------
        // Layout & Spacing
        // ------------------------------------------------

        // Padding
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;

        // Widget Sizes
        style.GrabMinSize = 10.0f;
        style.ScrollbarSize = 14.0f;

        // Rounding
        style.WindowRounding = 0.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 7.0f;
        style.TabRounding = 2.0f;

        // Borders
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;

        // Minimum Window Size
        style.WindowMinSize = ImVec2(160.0f, 100.0f);
    }
}
