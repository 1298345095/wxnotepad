# QuickJS-ng expression compatibility overlay

Recipe/source pin: vcpkg baseline `d186940b2f0c4e8e5b4ff804af57d2a26fbfa091`,
QuickJS-ng tag `v0.16.2`, upstream archive SHA-512 retained in `portfile.cmake`.
Port revision 1 adds `original-nan.patch`; application linkage remains static and
the optional libc modules remain disabled in the manifest.

The pinned Windows original system engine creates negative quiet NaNs. Its
propsys formatter preserves NaN signs (`-1.#IND` vs `1.#QNAN`), so merely printing
a fixed NaN string loses observable results of unary minus and Math operations.
The patch preserves value-level behavior through globals/Number, conversions,
variables and object returns. It sets the engine's canonical NaN and Number/global
NaN constants to the original sign, preserves original canonicalization by
abs/round/sign/min/max, and retains the independently captured domain-result signs
for acos/asin/acosh/atanh across the Linux and Windows math runtimes. All ordinary
finite/infinite and argument-conversion paths are retained; min/max still coerce
every argument before final NaN canonicalization. No expression-text inspection
or case-specific formatted-output substitution is used.

The independent original and standalone patched engine agree on 36 samples in
`292/nan-comparison.json` under the implementation evidence directory. The real
native/frame and packaged-application fixtures exercise the same values and
the original result insertion/history boundaries. The SDK's actual vcpkg SPDX
records retain the overlay files and patch provenance in the application bundle.
