import os
Import("env")

# Restore original working pattern from old lexepub linking
lib_path = os.path.join(
    env.subst("$PROJECT_DIR"),
    "lib", "honzo_c", "xtensa-esp32s3-espidf", "release"
)
env.Prepend(LIBPATH=[lib_path])
env.Prepend(LIBS=["honzo_c"])

# Both U8g2 @ ^2.36.4 and U8g2_for_Adafruit_GFX define identical font
# data symbols in their own copy of u8g2_fonts.c.  Linking both causes
# duplicate-strong-symbol errors.  Allow the linker to accept them
# (the first definition wins; both copies carry the same data).
env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])
