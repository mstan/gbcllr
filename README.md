# gbcllr

Shared Game Boy and Game Boy Color recompilation engine. Game repositories pin
an exact engine revision and keep their own identity, configuration and packaging.

The engine provides DMG/CGB machines, native instruction specialization,
build-selected host services, native saves, current-build checkpoints, rewind,
controller bindings, native enhancement packages and the shared recomp-ui launcher.
Coverage depends on the selected cartridge and qualified system profile.

## Source checkout

```sh
git clone --recurse-submodules https://github.com/mstan/gbcllr.git
```

Use a game consumer's supported build entry point to select its qualified HLE
profile and native presentation services:

- [Pokemon Red and Blue](https://github.com/mstan/PokemonRedAndBlueRecomp)
- [Pokemon Crystal](https://github.com/mstan/PokemonCrystalRecomp)

The engine uses CMake, Ninja and Python 3.11 or later. Desktop builds use SDL2
and OpenGL. Preserve the consumer's recorded engine and dependency revisions.

Original cartridges remain read-only inputs. Generated guest code, firmware,
native saves, local QA records and captures stay outside this source repository.
Guarded loaded-cartridge and guest-RAM writes, runtime hooks and content overlays
are supported operations; native saves remain separate from disposable states.

## License

The engine's authored code is available under the MIT license. Third-party
components retain their own notices; see [THIRD_PARTY.md](THIRD_PARTY.md).
