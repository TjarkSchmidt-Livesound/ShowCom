// Freistellen von Profilbildern mit Apple Vision (macOS 12+).
// Wird mit -fobjc-arc übersetzt (siehe CMakeLists.txt).

#include <juce_graphics/juce_graphics.h>

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

bool showcomVisionPersonMask (const juce::Image& image, juce::Image& maskOut)
{
    if (! image.isValid())
        return false;

    if (@available (macOS 12.0, *))
    {
        @autoreleasepool
        {
            juce::MemoryOutputStream png;
            if (! juce::PNGImageFormat().writeImageToStream (image, png))
                return false;

            NSData* data = [NSData dataWithBytes: png.getData() length: png.getDataSize()];
            VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithData: data options: @{}];

            VNGeneratePersonSegmentationRequest* request = [[VNGeneratePersonSegmentationRequest alloc] init];
            request.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelAccurate;
            request.outputPixelFormat = kCVPixelFormatType_OneComponent8;

            NSError* error = nil;
            if (! [handler performRequests: @[ request ] error: &error])
                return false;

            VNPixelBufferObservation* observation = request.results.firstObject;
            if (observation == nil)
                return false;

            CVPixelBufferRef buffer = observation.pixelBuffer;
            if (buffer == nullptr)
                return false;

            CVPixelBufferLockBaseAddress (buffer, kCVPixelBufferLock_ReadOnly);

            const int mw = (int) CVPixelBufferGetWidth (buffer);
            const int mh = (int) CVPixelBufferGetHeight (buffer);
            const size_t stride = CVPixelBufferGetBytesPerRow (buffer);
            const auto* base = static_cast<const uint8_t*> (CVPixelBufferGetBaseAddress (buffer));

            const int w = image.getWidth(), h = image.getHeight();
            juce::Image mask (juce::Image::ARGB, w, h, true);

            if (base != nullptr && mw > 1 && mh > 1)
            {
                juce::Image::BitmapData dst (mask, juce::Image::BitmapData::writeOnly);

                // Maske (meist kleiner als das Bild) bilinear auf Bildgröße bringen
                for (int y = 0; y < h; ++y)
                {
                    const float fy = juce::jlimit (0.0f, (float) (mh - 1), ((float) y + 0.5f) * (float) mh / (float) h - 0.5f);
                    const int y0 = (int) fy, y1 = juce::jmin (mh - 1, y0 + 1);
                    const float ty = fy - (float) y0;

                    for (int x = 0; x < w; ++x)
                    {
                        const float fx = juce::jlimit (0.0f, (float) (mw - 1), ((float) x + 0.5f) * (float) mw / (float) w - 0.5f);
                        const int x0 = (int) fx, x1 = juce::jmin (mw - 1, x0 + 1);
                        const float tx = fx - (float) x0;

                        auto px = [&] (int xx, int yy) { return (float) base[(size_t) yy * stride + (size_t) xx]; };
                        const float top = px (x0, y0) + (px (x1, y0) - px (x0, y0)) * tx;
                        const float bot = px (x0, y1) + (px (x1, y1) - px (x0, y1)) * tx;
                        const auto a = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (top + (bot - top) * ty));

                        dst.setPixelColour (x, y, juce::Colour::fromRGBA (255, 255, 255, a));
                    }
                }
            }

            CVPixelBufferUnlockBaseAddress (buffer, kCVPixelBufferLock_ReadOnly);

            if (base == nullptr)
                return false;

            maskOut = mask;
            return true;
        }
    }

    return false;
}
