#pragma once

#include "UncompressedVideoSampleProvider.h"
#include "TexturePool.h"
#include <DirectXInteropHelper.h>

extern "C"
{
#include <libavutil/hwcontext_d3d11va.h>
}

static AVPixelFormat get_format(struct AVCodecContext* s, const enum AVPixelFormat* fmt);

namespace FFmpegInteropX
{
    using namespace Platform;

    ref class D3D11VideoSampleProvider : UncompressedVideoSampleProvider
    {
    private:
        const AVCodec* hwCodec;
        const AVCodec* swCodec;

    internal:

        D3D11VideoSampleProvider(
            FFmpegReader^ reader,
            AVFormatContext* avFormatCtx,
            AVCodecContext* avCodecCtx,
            MediaSourceConfig^ config,
            int streamIndex,
            HardwareDecoderStatus hardwareDecoderStatus)
            : UncompressedVideoSampleProvider(reader, avFormatCtx, avCodecCtx, config, streamIndex, hardwareDecoderStatus)
        {
            decoder = DecoderEngine::FFmpegD3D11HardwareDecoder;
            hwCodec = avCodecCtx->codec;

            if (avCodecCtx->codec_id == AVCodecID::AV_CODEC_ID_AV1)
            {
                swCodec = avcodec_find_decoder_by_name("libdav1d");
            }
            else
            {
                swCodec = NULL;
            }
        }

    public:

        virtual ~D3D11VideoSampleProvider()
        {
            ReleaseTrackedSamples();
        }

        virtual void Flush() override
        {
            UncompressedVideoSampleProvider::Flush();
            ReturnTrackedSamples();
        }

    internal:

        virtual HRESULT CreateBufferFromFrame(IBuffer^* pBuffer, IDirect3DSurface^* surface, AVFrame* avFrame, int64_t& framePts, int64_t& frameDuration) override
        {
            HRESULT hr = S_OK;

            if (avFrame->format == AV_PIX_FMT_D3D11)
            {
                if (!texturePool)
                {
                    // init texture pool, fail if we did not get a device ptr
                    if (device && deviceContext)
                    {
                        texturePool = ref new TexturePool(device, 5);
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
                //copy texture data
                if (SUCCEEDED(hr))
                {
                    //cast the AVframe to texture 2D
                    auto decodedTexture = reinterpret_cast<ID3D11Texture2D*>(avFrame->data[0]);
                    ID3D11Texture2D* renderTexture = nullptr;
                    //happy path:decoding and rendering on same GPU
                    hr = texturePool->GetCopyTexture(decodedTexture, &renderTexture);
                    deviceContext->CopySubresourceRegion(renderTexture, 0, 0, 0, 0, decodedTexture, (UINT)(unsigned long long)avFrame->data[1], NULL);
                    deviceContext->Flush();

                    //create a IDXGISurface from the shared texture
                    IDXGISurface* finalSurface = NULL;
                    if (SUCCEEDED(hr))
                    {
                        hr = renderTexture->QueryInterface(&finalSurface);
                    }

                    //get the IDirect3DSurface pointer
                    if (SUCCEEDED(hr))
                    {
                        *surface = DirectXInteropHelper::GetSurface(finalSurface);
                    }

                    if (SUCCEEDED(hr))
                    {
                        CheckFrameSize(avFrame);
                        ReadFrameProperties(avFrame, framePts);
                    }

                    SAFE_RELEASE(finalSurface);
                    SAFE_RELEASE(renderTexture);
                    if (decoder != DecoderEngine::FFmpegD3D11HardwareDecoder)
                    {
                        decoder = DecoderEngine::FFmpegD3D11HardwareDecoder;
                        VideoInfo->DecoderEngine = decoder;
                    }
                }
            }
            else
            {
                hr = UncompressedVideoSampleProvider::CreateBufferFromFrame(pBuffer, surface, avFrame, framePts, frameDuration);

                if (decoder != DecoderEngine::FFmpegSoftwareDecoder)
                {
                    decoder = DecoderEngine::FFmpegSoftwareDecoder;
                    VideoInfo->DecoderEngine = decoder;
                }
            }

            return hr;
        }

        virtual HRESULT SetSampleProperties(MediaStreamSample^ sample) override
        {
            if (sample->Direct3D11Surface)
            {
                std::lock_guard<std::mutex> lock(samplesMutex);

                // AddRef the sample on native interface to prevent it from being collected before Processed is called
                auto sampleNative = reinterpret_cast<IUnknown*>(sample);
                sampleNative->AddRef();

                // Attach Processed event and store in samples list
                auto token = sample->Processed += ref new Windows::Foundation::TypedEventHandler<Windows::Media::Core::MediaStreamSample^, Platform::Object^>(this, &D3D11VideoSampleProvider::OnProcessed);
                trackedSamples[sampleNative] = token;
            }

            return UncompressedVideoSampleProvider::SetSampleProperties(sample);
        };

        void OnProcessed(Windows::Media::Core::MediaStreamSample^ sender, Platform::Object^ args)
        {
            std::lock_guard<std::mutex> lock(samplesMutex);

            auto sampleNative = reinterpret_cast<IUnknown*>(sender);
            auto mapEntry = trackedSamples.find(sampleNative);
            if (mapEntry == trackedSamples.end())
            {
                // sample was already released during Flush() or destructor
            }
            else
            {
                // Release the sample's native interface and return texture to pool
                sampleNative->Release();
                trackedSamples.erase(mapEntry);

                ReturnTextureToPool(sender);
            }
        }

        void ReturnTextureToPool(MediaStreamSample^ sample)
        {
            IDXGISurface* surface = NULL;
            ID3D11Texture2D* texture = NULL;

            HRESULT hr = DirectXInteropHelper::GetDXGISurface(sample->Direct3D11Surface, &surface);

            if (SUCCEEDED(hr))
            {
                hr = surface->QueryInterface(&texture);
            }

            if (SUCCEEDED(hr))
            {
                texturePool->ReturnTexture(texture);
            }

            SAFE_RELEASE(surface);
            SAFE_RELEASE(texture);
        }

        void ReleaseTrackedSamples()
        {
            std::lock_guard<std::mutex> lock(samplesMutex);
            for (auto entry : trackedSamples)
            {
                // detach Processed event and release native interface
                auto sampleNative = entry.first;
                auto sample = reinterpret_cast<MediaStreamSample^>(sampleNative);

                sample->Processed -= entry.second;
                sampleNative->Release();
            }

            trackedSamples.clear();
        }

        void ReturnTrackedSamples()
        {
            std::lock_guard<std::mutex> lock(samplesMutex);
            for (auto entry : trackedSamples)
            {
                // detach Processed event and release native interface
                auto sampleNative = entry.first;
                auto sample = reinterpret_cast<MediaStreamSample^>(sampleNative);

                sample->Processed -= entry.second;
                sampleNative->Release();

                // return texture to pool
                ReturnTextureToPool(sample);
            }

            trackedSamples.clear();
        }

        virtual HRESULT SetHardwareDevice(ID3D11Device* device, ID3D11DeviceContext* context, AVBufferRef* avHardwareContext) override
        {
            HRESULT hr = S_OK;

            bool isCompatible = false;
            hr = CheckHWAccelerationCompatible(device, isCompatible);

            if (SUCCEEDED(hr))
            {
                if (isCompatible)
                {
                    bool needsReinit = this->device;

                    SAFE_RELEASE(this->device);
                    SAFE_RELEASE(this->deviceContext);

                    device->AddRef();
                    context->AddRef();
                    this->device = device;
                    this->deviceContext = context;

                    if (needsReinit)
                    {
                        hr = UpdateCodecContext(hwCodec, avHardwareContext);
                    }
                    else if (!m_pAvCodecCtx->hw_device_ctx || m_pAvCodecCtx->hw_device_ctx->data != avHardwareContext->data)
                    {
                        av_buffer_unref(&m_pAvCodecCtx->hw_device_ctx);
                        m_pAvCodecCtx->hw_device_ctx = av_buffer_ref(avHardwareContext);
                    }
                }
                else
                {
                    SAFE_RELEASE(this->device);
                    SAFE_RELEASE(this->deviceContext);
                    av_buffer_unref(&m_pAvCodecCtx->hw_device_ctx);

                    if (swCodec)
                    {
                        hr = UpdateCodecContext(swCodec, NULL);
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
            }

            delete texturePool;
            texturePool = nullptr;

            return hr;
        }

        HRESULT UpdateCodecContext(const AVCodec* codec, AVBufferRef* avHardwareContext)
        {
            auto hr = S_OK;

            avcodec_free_context(&m_pAvCodecCtx);

            m_pAvCodecCtx = avcodec_alloc_context3(codec);
            if (!m_pAvCodecCtx)
            {
                hr = E_OUTOFMEMORY;
            }

            if (SUCCEEDED(hr))
            {
                // enable multi threading only for SW decoders
                if (!avHardwareContext)
                {
                    unsigned threads = std::thread::hardware_concurrency();
                    m_pAvCodecCtx->thread_count = m_config->MaxVideoThreads == 0 ? threads : min(threads, m_config->MaxVideoThreads);
                    m_pAvCodecCtx->thread_type = m_config->IsFrameGrabber ? FF_THREAD_SLICE : FF_THREAD_FRAME | FF_THREAD_SLICE;
                }

                // initialize the stream parameters with demuxer information
                hr = avcodec_parameters_to_context(m_pAvCodecCtx, m_pAvStream->codecpar);
            }

            if (SUCCEEDED(hr))
            {
                m_pAvCodecCtx->hw_device_ctx = avHardwareContext ? av_buffer_ref(avHardwareContext) : NULL;
                m_pAvCodecCtx->get_format = &get_format;
                hr = avcodec_open2(m_pAvCodecCtx, codec, NULL);
            }

            if (SUCCEEDED(hr))
            {
                frameProvider->UpdateCodecContext(m_pAvCodecCtx);
            }

            return hr;
        }

        HRESULT CheckHWAccelerationCompatible(ID3D11Device* device, bool& isCompatible)
        {
            HRESULT hr = S_OK;
            isCompatible = true;

            if (m_pAvCodecCtx->codec->id == AVCodecID::AV_CODEC_ID_AV1)
            {
                isCompatible = m_pAvCodecCtx->profile >= 0 && m_pAvCodecCtx->profile <= 2;
                if (isCompatible)
                {
                    auto const av1Profiles = new const GUID[]{
                        D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0,
                        D3D11_DECODER_PROFILE_AV1_VLD_PROFILE1,
                        D3D11_DECODER_PROFILE_AV1_VLD_PROFILE2
                    };
                    auto requiredProfile = av1Profiles[m_pAvCodecCtx->profile];

                    ID3D11VideoDevice* videoDevice = NULL;
                    hr = device->QueryInterface(&videoDevice);

                    // check profile exists
                    if (SUCCEEDED(hr))
                    {
                        isCompatible = false;
                        GUID profile;
                        int count = videoDevice->GetVideoDecoderProfileCount();
                        for (int i = 0; i < count; i++)
                        {
                            hr = videoDevice->GetVideoDecoderProfile(i, &profile);
                            if (FAILED(hr))
                            {
                                break;
                            }
                            else if (profile == requiredProfile)
                            {
                                isCompatible = true;
                                break;
                            }
                        }
                    }

                    // check resolution
                    if (SUCCEEDED(hr) && isCompatible)
                    {
                        D3D11_VIDEO_DECODER_DESC desc;
                        desc.Guid = requiredProfile;
                        desc.OutputFormat = DXGI_FORMAT_NV12;

                        desc.SampleWidth = m_pAvCodecCtx->coded_width;
                        desc.SampleHeight = m_pAvCodecCtx->coded_height;

                        UINT count;
                        hr = videoDevice->GetVideoDecoderConfigCount(&desc, &count);
                        if (SUCCEEDED(hr))
                        {
                            isCompatible = count > 0;
                        }
                    }

                    SAFE_RELEASE(videoDevice);
                }

                if (FAILED(hr))
                {
                    isCompatible = false;
                }
            }

            return hr;
        }

        static HRESULT InitializeHardwareDeviceContext(MediaStreamSource^ sender, AVBufferRef* avHardwareContext, ID3D11Device** outDevice, ID3D11DeviceContext** outDeviceContext, IMFDXGIDeviceManager* deviceManager, HANDLE* outDeviceHandle)
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deviceContext = nullptr;
            ID3D11VideoDevice* videoDevice = nullptr;
            ID3D11VideoContext* videoContext = nullptr;

            HRESULT hr = DirectXInteropHelper::GetDeviceFromStreamSource(deviceManager, &device, &deviceContext, &videoDevice, outDeviceHandle);

            if (SUCCEEDED(hr)) hr = deviceContext->QueryInterface(&videoContext);

            auto dataBuffer = (AVHWDeviceContext*)avHardwareContext->data;
            auto internalDirectXHwContext = (AVD3D11VADeviceContext*)dataBuffer->hwctx;

            if (SUCCEEDED(hr))
            {
                // give ownership to FFmpeg

                internalDirectXHwContext->device = device;
                internalDirectXHwContext->device_context = deviceContext;
                internalDirectXHwContext->video_device = videoDevice;
                internalDirectXHwContext->video_context = videoContext;
            }
            else
            {
                // release
                SAFE_RELEASE(device);
                SAFE_RELEASE(deviceContext);
                SAFE_RELEASE(videoDevice);
                SAFE_RELEASE(videoContext);
            }

            if (SUCCEEDED(hr))
            {
                // multithread interface seems to be optional
                ID3D10Multithread* multithread;
                device->QueryInterface(&multithread);
                if (multithread)
                {
                    multithread->SetMultithreadProtected(TRUE);
                    multithread->Release();
                }
            }

            if (SUCCEEDED(hr))
            {
                hr = av_hwdevice_ctx_init(avHardwareContext);
            }

            if (SUCCEEDED(hr))
            {
                // addref and hand out pointers
                device->AddRef();
                deviceContext->AddRef();
                *outDevice = device;
                *outDeviceContext = deviceContext;
            }

            return hr;
        }

        TexturePool^ texturePool;
        std::map<IUnknown*,EventRegistrationToken> trackedSamples;
        std::mutex samplesMutex;
    };
}

