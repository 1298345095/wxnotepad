// A real loadable system-service fixture with none of ICU's required exports.
// The original optional-metadata contract omits both fields on a missing export.
extern "C" int wxnotepad_unavailable_character_metadata() {
    return 0;
}
