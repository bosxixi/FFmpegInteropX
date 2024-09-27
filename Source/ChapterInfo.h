#pragma once
#include "ChapterInfo.g.h"

namespace winrt::FFmpegInteropX::implementation
{
    struct ChapterInfo : ChapterInfoT<ChapterInfo>
    {
        ChapterInfo(hstring const& title, Windows::Foundation::TimeSpan const& startTime, Windows::Foundation::TimeSpan const& duration);
        hstring Title();
        Windows::Foundation::TimeSpan StartTime();
        Windows::Foundation::TimeSpan Duration();

    private:
        hstring codecName{};
        hstring language{};
        int32_t index{};
        hstring imageType{};
        hstring title{};
        TimeSpan startTime{};
        TimeSpan duration{};
    };
}
namespace winrt::FFmpegInteropX::factory_implementation
{
    struct ChapterInfo : ChapterInfoT<ChapterInfo, implementation::ChapterInfo>
    {
    };
}
