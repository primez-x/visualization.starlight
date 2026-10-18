#pragma once
#include <array>
#include <cstddef>
#include <complex>

namespace starlight
{
class Spectrum
{
public:
  static constexpr size_t Window = 1024;
  static constexpr size_t Bars = 160;
  bool Reset(int channels, int sampleRate);
  void Push(const float* samples, size_t count);
  const std::array<float, Bars>& Levels() const { return m_levels; }
private:
  void Analyse();
  int m_channels = 0;
  size_t m_used = 0;
  std::array<float, Window * 8> m_pcm{};
  std::array<float, Window> m_window{};
  std::array<std::complex<float>, Window> m_fft{};
  std::array<std::complex<float>, Window / 2> m_roots{};
  std::array<size_t, Window> m_reverse{};
  std::array<size_t, Bars> m_first{}, m_last{};
  std::array<float, Bars> m_levels{};
};
}
