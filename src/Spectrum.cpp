#include "Spectrum.h"
#include <algorithm>
#include <cmath>

namespace starlight
{
bool Spectrum::Reset(int channels, int sampleRate)
{
  m_channels = 0;
  m_used = 0;
  m_levels.fill(0);
  if (channels < 1 || channels > 8 || sampleRate < 8000 || sampleRate > 384000)
    return false;
  m_channels = channels;
  constexpr float tau = 6.28318530718f;
  for (size_t n = 0; n < Window; ++n)
  {
    m_window[n] = .5f - .5f * std::cos(tau * n / (Window - 1));
    size_t source = n, reversed = 0;
    for (size_t bit = Window; bit > 1; bit >>= 1)
    {
      reversed = (reversed << 1) | (source & 1);
      source >>= 1;
    }
    m_reverse[n] = reversed;
  }
  for (size_t n = 0; n < m_roots.size(); ++n)
    m_roots[n] = std::polar(1.0f, -tau * n / Window);
  const float high = std::min(16000.0f, sampleRate * .48f);
  for (size_t bar = 0; bar < Bars; ++bar)
  {
    const float lowHz = 60 * std::pow(high / 60, static_cast<float>(bar) / Bars);
    const float highHz = 60 * std::pow(high / 60, static_cast<float>(bar + 1) / Bars);
    m_first[bar] = std::clamp<size_t>(lowHz * Window / sampleRate, 1, Window / 2 - 1);
    m_last[bar] = std::clamp<size_t>(std::ceil(highHz * Window / sampleRate), m_first[bar] + 1, Window / 2);
  }
  return true;
}

void Spectrum::Push(const float* samples, size_t count)
{
  if (!samples || m_channels == 0)
    return;
  const size_t block = Window * m_channels;
  for (size_t n = 0; n < count; ++n)
  {
    m_pcm[m_used++] = std::isfinite(samples[n]) ? std::clamp(samples[n], -1.0f, 1.0f) : 0;
    if (m_used == block)
    {
      Analyse();
      m_used = 0;
    }
  }
}

void Spectrum::Analyse()
{
  std::array<float, Window / 2> power{};
  // Combine channel power after the FFT to preserve opposite-phase stereo.
  for (int channel = 0; channel < m_channels; ++channel)
  {
    float mean = 0;
    for (size_t n = 0; n < Window; ++n)
      mean += m_pcm[n * m_channels + channel];
    mean /= Window;
    for (size_t n = 0; n < Window; ++n)
      m_fft[m_reverse[n]] = (m_pcm[n * m_channels + channel] - mean) * m_window[n];
    for (size_t span = 2; span <= Window; span *= 2)
    {
      const size_t half = span / 2;
      for (size_t base = 0; base < Window; base += span)
        for (size_t n = 0; n < half; ++n)
        {
          const auto odd = m_fft[base + n + half] * m_roots[n * Window / span];
          const auto even = m_fft[base + n];
          m_fft[base + n] = even + odd;
          m_fft[base + n + half] = even - odd;
        }
    }
    for (size_t bin = 1; bin < power.size(); ++bin)
      power[bin] = std::max(power[bin], std::norm(m_fft[bin]));
  }
  constexpr float normalization = 16.0f / (Window * Window);
  for (size_t bar = 0; bar < Bars; ++bar)
  {
    float peak = 0;
    for (size_t bin = m_first[bar]; bin < m_last[bar]; ++bin)
      peak = std::max(peak, power[bin]);
    const float decibels = 10 * std::log10(std::max(peak * normalization, 1e-10f));
    m_levels[bar] = std::clamp((decibels + 65) / 60, 0.0f, 1.0f);
  }
}
}
