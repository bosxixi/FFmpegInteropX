#include "pch.h"
#include "BasicVideoEffect.h"
#include "BasicVideoEffect.g.cpp"
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Media::Effects;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Media::MediaProperties;

using namespace winrt::Microsoft::Graphics;
using namespace winrt::Microsoft::Graphics::Canvas;
using namespace winrt::Microsoft::Graphics::Canvas::Effects;
// WARNING: This file is automatically generated by a tool. Do not directly
// add this file to your project, as any changes you make will be lost.
// This file is a stub you can use as a starting point for your implementation.
//
// To add a copy of this file to your project:
//   1. Copy this file from its original location to the location where you store 
//      your other source files (e.g. the project root). 
//   2. Add the copied file to your project. In Visual Studio, you can use 
//      Project -> Add Existing Item.
//   3. Delete this comment and the 'static_assert' (below) from the copied file.
//      Do not modify the original file.
//
// To update an existing file in your project:
//   1. Copy the relevant changes from this file and merge them into the copy 
//      you made previously.
//    
// This assertion helps prevent accidental modification of generated files.
//static_assert(false, "This file is generated by a tool and will be overwritten. Open this error and view the comment for assistance.");

namespace winrt::FFmpegInteropXWinUI::implementation
{
    void BasicVideoEffect::SetProperties(Windows::Foundation::Collections::IPropertySet const& configuration)
    {
        inputConfiguration = configuration;
        effectConfiguration = winrt::unbox_value< winrt::FFmpegInteropXWinUI::VideoEffectConfiguration>(configuration.Lookup(L"config"));
    }

    bool BasicVideoEffect::IsReadOnly()
    {
        return false;
    }

    Windows::Media::Effects::MediaMemoryTypes BasicVideoEffect::SupportedMemoryTypes()
    {
        return winrt::Windows::Media::Effects::MediaMemoryTypes::Gpu;
    }

    bool BasicVideoEffect::TimeIndependent()
    {
        return true;
    }

    Windows::Foundation::Collections::IVectorView<Windows::Media::MediaProperties::VideoEncodingProperties> BasicVideoEffect::SupportedEncodingProperties()
    {
        auto encodingProperties = VideoEncodingProperties();
        encodingProperties.Subtype(L"ARGB32");
        auto vector = winrt::single_threaded_vector<VideoEncodingProperties>();
        vector.Append(encodingProperties);
        return vector.GetView();
    }

    void BasicVideoEffect::SetEncodingProperties(Windows::Media::MediaProperties::VideoEncodingProperties const& encodingProperties, Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device)
    {
        UNREFERENCED_PARAMETER(encodingProperties);
        canvasDevice = CanvasDevice::CreateFromDirect3D11Device(device);
    }

    void BasicVideoEffect::ProcessFrame(Windows::Media::Effects::ProcessVideoFrameContext const& context)
    {
        try
        {
            auto c = effectConfiguration.Contrast();
            auto b = effectConfiguration.Brightness();
            auto s = effectConfiguration.Saturation();
            auto temp = effectConfiguration.Temperature();
            auto tint = effectConfiguration.Tint();
            auto sharpness = effectConfiguration.Sharpness();
            auto sharpnessThreshold = effectConfiguration.SharpnessThreshold();

            bool hasSharpness = sharpness > 0.0f;
            bool hasColor = c != 0.0f || b != 0.0f || s != 0.0f;
            bool hasTemperatureAndTint = tint != 0.0f || temp != 0.0f;

            ICanvasImage source = CanvasBitmap::CreateFromDirect3D11Surface(canvasDevice, context.InputFrame().Direct3DSurface());

            if (hasColor)
            {
                source = CreateColorEffect(source, c + 1.0f, b, s + 1.0f);
            }

            if (hasTemperatureAndTint)
            {
                source = CreateTermperatureAndTintEffect(source, temp, tint);
            }

            if (hasSharpness)
            {
                source = CreateSharpnessEffect(source, sharpness, sharpnessThreshold);
            }

            auto renderTarget = CanvasRenderTarget::CreateFromDirect3D11Surface(canvasDevice, context.OutputFrame().Direct3DSurface());
            auto ds = renderTarget.CreateDrawingSession();
            ds.Antialiasing(CanvasAntialiasing::Aliased);
            ds.Blend(CanvasBlend::Copy);
            ds.DrawImage(source);
        }
        catch (...)
        {
        }
    }

    void BasicVideoEffect::Close(Windows::Media::Effects::MediaEffectClosedReason const& reason)
    {
        UNREFERENCED_PARAMETER(reason);
        canvasDevice = nullptr;
    }

    void BasicVideoEffect::DiscardQueuedFrames()
    {
    }
}