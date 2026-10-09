# Offline DWG document loading

The generic `ICadDocumentImportService` in the CAD import module decodes DXF
documents and loads DWG through the unmodified, independent LibreDWG command
line process. It exchanges standard DXF files with that process, then returns
neutral header/block/entity records. The service has no product, scene or
editable-sketch dependency. LibreDWG is never linked into an iCAX module.

The pinned upstream Windows x64 release is LibreDWG 0.14.8601. It includes
GNU libiconv 1.18 (`_libiconv_version=0x0112`). `libredwg.json` records upstream
URLs, archive SHA-256 and the exact runtime file allowlist. Runtime loading
never downloads files and never searches `PATH`. The converter is located at
`runtime/cad/libredwg` relative to the CAD import module.

From a clean checkout, prepare and deploy with:

```powershell
src/tools/build/prepare_sketch_cad_runtime.ps1 -OutputDirectory src/x64/Debug -Download
```

Subsequent builds deploy the verified local cache without network access.
`-ValidateOnly` checks deployed hashes and the exact allowlist. The release
packager uses the same manifest; local drawings and user data are not inputs.
CI may select an isolated preparation cache with `-CacheDirectory`.

The neutral result contains `header` (variable name to ordered `[code,value]`
pairs) and `sections` (section name to ordered `{type,values}` records), with
`sourceFormat` identifying the input exchange encoding. Numeric values are
decimal strings and text is UTF-8. Block, INSERT, POLYLINE/VERTEX/SEQEND and
layer table ordering is preserved; a THUMBNAILIMAGE section's direct groups
are held in a same-named metadata record. There are no sketch IDs, cut/fill
properties, product placement, resource references or millimetre scaling.

LibreDWG is GPL-3.0-or-later; GNU libiconv is LGPL-2.1-or-later. The deployment
includes both license texts and the complete corresponding upstream source
archives beside the separate converter, rather than depending on a future
network download. A recipient may replace or rebuild the independent tool.
No signing or application authorization is required to use or modify it.

The LibreDWG source archive has generated Autotools files. Rebuild it with a
MinGW-w64/MSYS2 C compiler, GNU make and libtool:

```sh
tar -xf libiconv-1.18.tar.gz
cd libiconv-1.18
./configure --prefix="$PWD/../install" --enable-shared --disable-static
make && make install
cd ..
tar -xf libredwg-0.14.8601.tar.xz
cd libredwg-0.14.8601
CPPFLAGS="-I$PWD/../install/include" LDFLAGS="-L$PWD/../install/lib" \
  ./configure --prefix="$PWD/../install" --enable-shared --disable-static
make && make install
```

The pinned upstream binaries, rather than a locally rebuilt variant, are the
release inputs. A rebuilt variant requires explicit metadata/hash updates and
the real file import regression. Autotools details are in the archive's
`INSTALL`/`README`; upstream Windows packaging uses MinGW. DWG conversion is
bounded to 30 seconds, 1 GiB process memory and 32 MiB source/output documents.
Unsupported or corrupt drawings fail explicitly.
