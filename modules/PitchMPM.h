#include "JuceHeader.h"
#include <float.h>
#include <complex>

#if JUCE_IOS || JUCE_MAC // TODO: Move this to Projucer project
#define AUDIOFFT_APPLE_ACCELERATE 1
#endif

//#include "AudioFFT.h"

#define CUTOFF 0.93 //0.97 is default
#define SMALL_CUTOFF 0.5
#define LOWER_PITCH_CUTOFF 80 //hz

class PitchMPM
{

public:

    PitchMPM(size_t bufferSize) : PitchMPM(44100, bufferSize) {}

    PitchMPM(int sampleRate, size_t bufferSize) : bufferSize (bufferSize),
                                                  sampleRate (sampleRate),
                                                  fftSize (2 * bufferSize), // Needs to be a power of 2!
                                                  //real (audiofft::AudioFFT::ComplexSize(fftSize)),
                                                  //imag (audiofft::AudioFFT::ComplexSize(fftSize)),
                                                  output (fftSize)


    {
        nsdf.insertMultiple(0, 0.0, bufferSize);

    }
    
    
    ~PitchMPM()
    {
        nsdf.clear();
        maxPositions.clear();
        ampEstimates.clear();
        periodEstimates.clear();
        
    }
    
    float getPitch(const float *audioBuffer)
    {
        
        float pitch;
        
        maxPositions.clearQuick();
        periodEstimates.clearQuick();
        ampEstimates.clearQuick();
        
        nsdfTimeDomain(audioBuffer);
        //nsdf = Array<float> (nsdfFrequencyDomain(audioBuffer).data());
        //nsdfFrequencyDomain(audioBuffer);

        peakPicking();
        
        float highestAmplitude = -FLT_MAX;
        
        for (auto tau : maxPositions)
        {
            highestAmplitude = jmax(highestAmplitude, nsdf.getUnchecked(tau));
            
            if (nsdf[tau] > SMALL_CUTOFF)
            {
                parabolicInterpolation (tau);
                ampEstimates.add (turningPointY);
                periodEstimates.add (turningPointX);
                highestAmplitude = jmax (highestAmplitude, turningPointY);
            }
        }
        
        if (periodEstimates.size() == 0)
        {
            pitch = -1;
        }
        else
        {
            float actualCutoff = CUTOFF * highestAmplitude;
            
            int periodIndex = 0;
            for (int i = 0; i < ampEstimates.size(); i++)
            {
                if (ampEstimates[i] >= actualCutoff)
                {
                    periodIndex = i;
                    break;
                }
            }
            
            float period = periodEstimates[periodIndex];
            float pitchEstimate = (sampleRate / period);
            if (pitchEstimate > LOWER_PITCH_CUTOFF)
            {
                pitch = pitchEstimate;
            }
            else
            {
                pitch = -1;
            }
        }
        return pitch;
    }
    
    void setSampleRate (int newSampleRate)
    {
        sampleRate = newSampleRate;
    }

private:
    size_t bufferSize;

    //audiofft::AudioFFT fft;
    size_t fftSize;
    std::vector<float> input;
    std::vector<float> real;
    std::vector<float> imag;
    std::vector<float> output;

    float sampleRate;
    
    float turningPointX, turningPointY;
    Array<float> nsdf;
    
    Array<int> maxPositions;
    Array<float> periodEstimates;
    Array<float> ampEstimates;
    
    void parabolicInterpolation(int tau)
    {
        float nsdfa = nsdf.getUnchecked (tau - 1);
        float nsdfb = nsdf.getUnchecked (tau);
        float nsdfc = nsdf.getUnchecked (tau + 1);
        float bValue = tau;
        float bottom = nsdfc + nsdfa - 2 * nsdfb;
        if (bottom == 0.0)
        {
            turningPointX = bValue;
            turningPointY = nsdfb;
        }
        else
        {
            float delta = nsdfa - nsdfc;
            turningPointX = bValue + delta / (2 * bottom);
            turningPointY = nsdfb - delta * delta / (8 * bottom);
        }
    }
    
    void peakPicking()
    {
        
        int pos = 0;
        int curMaxPos = 0;
        float* nsdfPtr = nsdf.getRawDataPointer();
        
        while (pos < (bufferSize - 1) / 3 && nsdfPtr[pos] > 0) {
            pos++;
        }
        
        while (pos < bufferSize - 1 && nsdfPtr[pos] <= 0.0) {
            pos++;
        }
        
        if (pos == 0) {
            pos = 1;
        }
        
        while (pos < bufferSize - 1) {
            if (nsdfPtr[pos] > nsdfPtr[pos - 1] && nsdfPtr[pos] >= nsdfPtr[pos + 1]) {
                if (curMaxPos == 0) {
                    curMaxPos = pos;
                } else if (nsdfPtr[pos] > nsdfPtr[curMaxPos]) {
                    curMaxPos = pos;
                }
            }
            pos++;
            if (pos < bufferSize - 1 && nsdfPtr[pos] <= 0) {
                if (curMaxPos > 0) {
                    maxPositions.add (curMaxPos);
                    curMaxPos = 0;
                }
                while (pos < bufferSize - 1 && nsdfPtr[pos] <= 0.0f) {
                    pos++;
                }
            }
        }
        if (curMaxPos > 0)
        {
            maxPositions.add (curMaxPos);
        }
    }
    
    void nsdfTimeDomain(const float *audioBuffer)
    {
        int tau;
        for (tau = 0; tau < bufferSize; tau++) {
            float acf = 0;
            float divisorM = 0;
            for (int i = 0; i < bufferSize - tau; i++) {
                acf += audioBuffer[i] * audioBuffer[i + tau];
                divisorM += audioBuffer[i] * audioBuffer[i] + audioBuffer[i + tau] * audioBuffer[i + tau];
            }
            nsdf.setUnchecked(tau, 2 * acf / divisorM);
        }
    }


//    std::vector<float> nsdfFrequencyDomain (const float *audioBuffer)
//    {
//        int size = bufferSize;
//        int size2 = 2*size-1;
//
//        //std::vector<std::complex<float>> acf(size2);
//        std::vector<float> acf_real(size2/2);
//
//
//        std::vector<float> acf (autoCorrelation (audioBuffer));
//
//        /*
//        for (auto it = acf.begin() + size2/2; it != acf.end(); ++it)
//            acf_real.push_back((*it) / acf[size2 / 2]);
//        */
//
//        for (int i = size2/2; i < acf.size(); ++i)
//            nsdf.setUnchecked(i, acf[i] / acf[size2 / 2]);
//
//
//        return acf_real;
//    }
//
//    std::vector<float> autoCorrelation(const float *audioBuffer)
//    {
//
//        std::vector<float> input (audioBuffer, audioBuffer + bufferSize);
//        input.resize(fftSize, 0.0f);
//
//        fft.init(fftSize);
//        fft.fft(input.data(), real.data(), imag.data());
//
//        // Complex Conjugate
//        for (int i = 0; i < fftSize; ++i)
//        {
//            /**
//             * std::complex method
//             */
//            std::complex<float> complex(real[i], imag[i]);
//            complex = complex * std::conj(complex); // No need to scale as AudioFFT does this already
//            real[i] = complex.real();
//            imag[i] = complex.imag();
//
//            /**
//             * calculate via real[i] * real[i] + imag[i] * imag[i].
//             * And if you really mean complex conjugation, just negate imag[i]
//             */
//
//            //imag[i] *= -1;
//            //real[i] = real[i] * real[i]; // + imag[i] * imag[i];
//        }
//
//        fft.ifft(output.data(), real.data(), imag.data());
//        return output;
//    }
//


};
