#pragma once
#include "GeometryServices/PlaneGeometryData.h"
#include "Services/IService.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace iCAX::GeometryServices {
struct PlaneArtworkBitmap {
  std::vector<std::uint8_t> EncodedPng;
  std::uint32_t PixelWidth = 0, PixelHeight = 0;
  double WidthMm = 0, HeightMm = 0;
};
// Pure artwork computation; identity, placement, publication and undo belong
// to the caller. Source bytes and all options are immutable snapshots.
class IPlaneArtworkService : public Services::IService {
public:
  using Map = PlaneGeometryData::Map;
  virtual PlaneArtworkBitmap NormalizeImage(const std::vector<std::uint8_t>& source) const = 0;
  virtual PlaneArtworkBitmap RasterizeText(const Map& options) const = 0;
  virtual Map Vectorize(const std::vector<std::uint8_t>& source, const Map& options,
                        const std::function<bool()>& cancelled = {}) const = 0;
};
} // namespace iCAX::GeometryServices
