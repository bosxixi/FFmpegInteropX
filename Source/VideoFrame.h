#pragma once
#include "VideoFrame.g.h"
#include "NativeBuffer.h"
#include <winrt/Windows.Graphics.Imaging.h>

using namespace winrt::Windows::Graphics::Imaging;
using namespace NativeBuffer;

namespace winrt::FFmpegInteropX::implementation
{
    struct VideoFrame : VideoFrameT<VideoFrame>
    {
        VideoFrame(Windows::Storage::Streams::IBuffer const& pixelData, uint32_t width, uint32_t height, Windows::Media::MediaProperties::MediaRatio const& pixelAspectRatio, Windows::Foundation::TimeSpan const& timestamp);
        ~VideoFrame();
        void Close();
        Windows::Storage::Streams::IBuffer PixelData();
        uint32_t PixelWidth();
        uint32_t PixelHeight();
        Windows::Media::MediaProperties::MediaRatio PixelAspectRatio();
        Windows::Foundation::TimeSpan Timestamp();
        Windows::Foundation::IAsyncAction EncodeAsBmpAsync(Windows::Storage::Streams::IRandomAccessStream stream);
        Windows::Foundation::IAsyncAction EncodeAsJpegAsync(Windows::Storage::Streams::IRandomAccessStream stream);
        Windows::Foundation::IAsyncAction EncodeAsPngAsync(Windows::Storage::Streams::IRandomAccessStream stream);
        uint32_t DisplayWidth();
        uint32_t DisplayHeight();
        double DisplayAspectRatio();

    private:
        winrt::Windows::Storage::Streams::IBuffer pixelData = { nullptr };
        unsigned int pixelWidth = 0;
        unsigned int pixelHeight = 0;
        winrt::Windows::Foundation::TimeSpan timestamp{};
        winrt::Windows::Media::MediaProperties::MediaRatio pixelAspectRatio = { nullptr };
        bool hasNonSquarePixels = false;

        winrt::Windows::Foundation::IAsyncAction Encode(winrt::Windows::Storage::Streams::IRandomAccessStream stream, winrt::guid encoderGuid)
        {
            auto strong = get_strong();

            // Retrieve the buffer data.  
            byte* pixels = pixelData.data();
            auto length = pixelData.Length();

            auto encoderValue = co_await winrt::Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(encoderGuid, stream);

            if (hasNonSquarePixels)
            {
                encoderValue.BitmapTransform().ScaledWidth(DisplayWidth());
                encoderValue.BitmapTransform().ScaledHeight(DisplayHeight());
                encoderValue.BitmapTransform().InterpolationMode(BitmapInterpolationMode::Linear);
            }

            encoderValue.SetPixelData(
                BitmapPixelFormat::Bgra8,
                BitmapAlphaMode::Straight,
                pixelWidth,
                pixelHeight,
                72,
                72,
                array_view(pixels, length));

            co_await encoderValue.FlushAsync();
        }

    };
}
namespace winrt::FFmpegInteropX::factory_implementation
{
    struct VideoFrame : VideoFrameT<VideoFrame, implementation::VideoFrame>
    {
    };
}
