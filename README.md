# Xtreme Sounds — VCV Rack Modules

Free/open-source VCV Rack 2 modules by **Xtreme Sounds / Chris DuPont**.

This repository builds one VCV plugin package named **XtremeSounds** containing multiple modules. New free Xtreme Sounds modules can be added to this collection later without changing the existing module slugs.

## Included modules

### S8 Quantizer

A 6HP polyphonic V/Oct quantizer with eight root/scale patterns, clock sequencing, theory-ranked harmony suggestions, intelligent fill, Auto-follow HOME, and four panel themes.

Full manual: [`docs/S8.md`](docs/S8.md)

### Fire Host

An Akai Fire controller interface for standalone Rack with polyphonic V/OCT/GATE/velocity outputs, Fire pad modes, 16 selectable RGB pad colors, Moss-style Root/Scale Play settings, relative encoders, and 20 assignable CV outputs.

Full manual: [`docs/FireHost.md`](docs/FireHost.md)

## Plugin identity

- Plugin slug: `XtremeSounds`
- Plugin name / brand: `Xtreme Sounds`
- Author: `Chris DuPont`
- Module slugs: `S8`, `FireHost`
- License: `GPL-3.0-or-later`

Do not rename the plugin slug or module slugs after release; Rack patches identify modules by plugin/module identity.

### Pre-release migration note

Earlier standalone development builds used plugin slugs `S8` and `FireHost`. The combined Library release uses plugin slug `XtremeSounds`. Patches saved with those standalone pre-release plugin identities will not automatically resolve to the combined collection; replace/re-add the module once and resave the patch. After the first VCV Library release, keep `XtremeSounds`, `S8`, and `FireHost` unchanged.

## Build

Normal Rack build:

```text
make
make dist
```

The included GitHub Actions workflow performs a Windows x64 validation build using the official Rack SDK 2.6.6 and the standard VCV Rack `make` / `make dist` process.

Push the contents of this folder to the repository root, then run **Actions > Build Xtreme Sounds VCV Plugin > Run workflow**.

Expected validation package:

```text
XtremeSounds-2.1.5-win-x64.vcvplugin
```

## VCV Library submission

This collection is open-source GPL-3.0-or-later. For an initial VCV Library submission, create exactly one issue in the VCVRack/library issue tracker with the title equal to the plugin slug:

```text
XtremeSounds
```

Post the public source repository URL. For later updates, increment `plugin.json` version, push the release commit, and post the new version and exact commit hash in the same plugin thread.

See [`VCV-LIBRARY-SUBMISSION.md`](VCV-LIBRARY-SUBMISSION.md).

## License

GNU GPL v3 or later. See `LICENSE.txt`, `NOTICE.txt`, and `THIRD_PARTY_NOTICES.txt`.

## GitHub build validation

The included `.github/workflows/build-vcv.yml` performs a VCV Rack 2 Windows x64 validation build using the official Rack SDK 2.6.6 and the standard `make` / `make dist` plugin build. The VCV Library maintainer builds accepted open-source plugins for supported platforms from the submitted source repository; this repository does not need four independent native GitHub runner builds for Library submission.
