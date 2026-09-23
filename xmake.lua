set_languages("cxx17")

target("tf2_360_qol_patches")
    set_kind("binary")
    add_files("src/*.cpp")
    -- static MSVC runtime, so release builds run without the VC++ Redistributable
    set_runtimes("MT")
