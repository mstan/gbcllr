# Third-party sources

Exact identities and imported paths are in `donors.lock.json`.

- SameBoy Core, copyright Lior Halphon and contributors, under the Expat license
  in `third_party/sameboy/LICENSE`. The original source is vendored from its
  committed revision, excluding an unrelated local APU modification.
- rbengine, under `third_party/rbengine/LICENSE`. Used for offline rewind and
  the engine-owned rollback snapshot ring.
- The optional system SHA256 service uses Windows BCrypt, a discovered
  Linux OpenSSL Crypto library, or the opt-in app-local Windows x64 OpenSSL
  service. No OpenSSL source is vendored. Qualified Linux libraries are
  OpenSSL 3.0.13 and 3.4.0; the bounded Windows package uses 3.6.0. These use
  the Apache 2.0 license; a package
  that bundles the library must include its corresponding license/notices.
  See <https://github.com/openssl/openssl/blob/openssl-3.4.0/LICENSE.txt>.
  Builds without that optional dependency retain the portable implementation.
- recomp-net, a pinned Git submodule under `third_party/recomp-net`, under its
  MIT `LICENSE`. The optional connected host uses its LAN input transport,
  capability/profile setup, rollback driver and confirmed-state protocol.
  The pin includes a paused-confirmation correction: polling publishes an
  already-agreed hash-chain frontier even when no new guest frame runs. A
  reached hold also waits for its final digest before announcing a drain.
- recomp-ui, a pinned Git submodule under `third_party/recomp-ui`, under its
  MIT `LICENSE`, supplies the shared setup/settings/input launcher and runtime
  menu, bundled Dear ImGui and licensed fonts/assets. Its pin includes the
  UTF-8 byte-view correction needed by C++20 consumers (draft upstream PR126)
  and responsive native-mod option controls (stacked draft PR128).
- Xtreme 2's native adapter and map reader adapt Matthew Stanley's authored
  `mmx2_adaptive.c` and `mmx2_map.h` from `mstan/MegamanXtreme2Recompiled`
  revision `e4649e231cc239a0f220f8afe0ae996916e0c6cf` into per-machine C++
  state and the SameBoy bus/palette contract. These authored adapter portions
  are available under this project's MIT license. SameBoy and other donor
  components retain their separate notices.

Changes to SameBoy include instance-owned random state, guarded generated
instruction/block dispatch, and a shared instruction-end helper that preserves
device/input boundaries when chaining instructions. Serial callback timing
records the donor timer phase without changing device batching. The host
adapter implements bounded MBC3 battery parsing
and serialization to keep wall-clock access out of simulation state I/O.
Zero-length initialization of absent cartridge RAM is skipped.
Hardware access timing and opcode semantics remain donor
implementations. Generated functions specialize those semantics for known ROM
instruction sites; their output is private ROM-derived build data.

The maintained gb-recompiled fork, SNESRecomp, PSXRecomp and GBARecomp supplied
architectural references. Their full implementations are not claimed as ported.
The missing root license in the local gb-recompiled fork must be resolved before
copying its implementation files into a redistributable build.

No Nintendo ROMs, boot ROMs, native saves or generated title code are included.
