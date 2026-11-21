// Independent checked FlatBuffer field reader for the public ICRT/ICRM wire
// contract. It does not call the production encoder/parser as its oracle.
struct ArtworkFlatBufferView {
  std::span<const std::uint8_t> Bytes;
  void RequireRange(std::size_t at, std::size_t count) const { Check(at <= Bytes.size() && count <= Bytes.size() - at, "artwork FlatBuffer field escaped its immutable buffer"); }
  std::uint16_t U16(std::size_t at) const { RequireRange(at, 2);return static_cast<std::uint16_t>(Bytes[at]) | static_cast<std::uint16_t>(Bytes[at + 1]) << 8; }
  std::uint32_t U32(std::size_t at) const { RequireRange(at, 4);std::uint32_t result = 0;for (int i = 0; i < 4; ++i) result |= static_cast<std::uint32_t>(Bytes[at + i]) << (8 * i);return result; }
  std::uint64_t U64(std::size_t at) const { RequireRange(at, 8);std::uint64_t result = 0;for (int i = 0; i < 8; ++i) result |= static_cast<std::uint64_t>(Bytes[at + i]) << (8 * i);return result; }
  void Identifier(const char *expected) const { RequireRange(4, 4);Check(std::equal(expected, expected + 4, Bytes.begin() + 4), "artwork ResourceAccess returned the wrong FlatBuffer identifier"); }
  std::size_t Field(std::size_t index) const {
    const std::size_t table = U32(0);RequireRange(table, 4);
    const auto vtableSigned = static_cast<std::int64_t>(table) - static_cast<std::int32_t>(U32(table));
    Check(vtableSigned >= 0, "artwork FlatBuffer vtable points before its buffer");const auto vtable = static_cast<std::size_t>(vtableSigned);
    const auto entry = 4 + index * 2, length = static_cast<std::size_t>(U16(vtable));RequireRange(vtable, length);
    if (entry + 2 > length) return 0;const auto offset = U16(vtable + entry);if (!offset) return 0;RequireRange(table + offset, 1);return table + offset;
  }
  std::uint32_t UInt(std::size_t field, std::uint32_t fallback = 0) const { const auto at = Field(field);return at ? U32(at) : fallback; }
  std::uint64_t ULong(std::size_t field) const { const auto at = Field(field);return at ? U64(at) : 0; }
  std::vector<std::uint8_t> Vector(std::size_t field) const { const auto at = Field(field);Check(at != 0, "artwork FlatBuffer vector missing");const auto start = at + U32(at), count = static_cast<std::size_t>(U32(start));RequireRange(start + 4, count);return {Bytes.begin() + start + 4, Bytes.begin() + start + 4 + count}; }
  Text String(std::size_t field) const { const auto value = Vector(field);return Text(value.begin(), value.end()); }
};

void CheckArtworkDisplayTexture(iCAX::Project::CProjectScene &scene, const Map &row,
                               const std::filesystem::path &dumpPath = {}) {
  const auto textureRef = row.at("textureRef").To<Map>(), bitmapRef = row.at("imageRef").To<Map>(), materialRef = row.at("materialRef").To<Map>();
  Check(textureRef.at("url") != bitmapRef.at("url"), "algorithm bitmap and display texture must remain distinct typed resources");
  iCAX::Resource::CResourceAccessService access(scene.Resources());
  const auto get = [&](const Map &ref, const Text &accept) {
    iCAX::Resource::CResourceRequest request;request.Method = iCAX::Resource::EResourceMethod::Get;request.URL = ref.at("url").To<Text>();
    request.Headers = {{"Accept", accept}, {"ICAX-Resource-Version", ref.at("version").To<Text>()}};return access.Request(request);
  };
  const auto texture = get(textureRef, "application/vnd.icax.flatbuffer");
  Check(texture.nStatus == 200 && texture.HasBody(), "the real framework ResourceAccess must serve display textures as FlatBuffer resources");
  const ArtworkFlatBufferView textureView{texture.Body.Bytes()};textureView.Identifier("ICRT");
  Check(textureView.UInt(0, 1) == 1 && textureView.ULong(1) > 0 && textureView.String(2) == "image/png" &&
            textureView.UInt(3) == row.at("pixelWidth").To<UInt>() && textureView.UInt(4) == row.at("pixelHeight").To<UInt>(),
        "ICRT must carry its schema/data versions, media type and actual typed artwork pixel dimensions");
  const auto bitmap = scene.Resources().Get<iCAX::Resource::CBinaryResource>(bitmapRef.at("url").To<Text>(), std::stoull(bitmapRef.at("version").To<Text>()));
  Check(bitmap && textureView.Vector(5) == bitmap->Content, "ICRT encoded PNG bytes must be exactly the immutable algorithm bitmap, not a placeholder or substituted image");
  Check(get(bitmapRef, "*/*").nStatus == 406, "binary algorithm bitmaps must stay outside the framework's FlatBuffer-only GET boundary");
  const auto material = get(materialRef, "application/vnd.icax.flatbuffer");Check(material.nStatus == 200, "artwork material must use the standard ResourceAccess representation");
  const ArtworkFlatBufferView materialView{material.Body.Bytes()};materialView.Identifier("ICRM");
  Check(materialView.String(9) == textureRef.at("url").To<Text>() && (materialView.UInt(8) & 3u) == 3u,
        "the unlit textured ICRM must reference typed display TextureRef, never the private algorithm BitmapRef");
  if (!dumpPath.empty()) {
    std::ofstream output(dumpPath, std::ios::binary);output.write(reinterpret_cast<const char *>(texture.Body.Data()), static_cast<std::streamsize>(texture.Body.Size()));
    Check(static_cast<bool>(output), "ICRT protocol test artifact write failed");std::cerr << "[artwork display texture] " << UTF8(dumpPath) << '\n' << std::flush;
  }
}
