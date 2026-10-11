`encodings.json` is an unmodified snapshot of the WHATWG Encoding Standard's
label registry, downloaded from <https://encoding.spec.whatwg.org/encodings.json>
on 2026-10-11. Its license is preserved in `LICENSE`.

`index-x-mac-cyrillic.txt` is the unmodified index from
<https://encoding.spec.whatwg.org/index-x-mac-cyrillic.txt>, downloaded on the same
date under the same license. It supplies the exact web mapping instead of the
different platform MACCYRILLIC mapping. `x-user-defined` needs no table.

CMake runs `generate.py` to produce sorted, read-only C lookup tables in the
build directory. No network access or generated source edits are needed to build.
To update the data, replace the corresponding source file from its URL, review
its diff, rebuild and run `encoding_test` plus `form_data_test`. The label registry
does not supply or certify the mappings of encodings still handled by iconv.
