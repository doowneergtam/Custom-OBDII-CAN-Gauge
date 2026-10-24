# Runtime fonts, both optional. Drop the TTFs into this folder to compile them in:
#  - Montserrat-Light.ttf (OFL, Google Fonts): digits above 48 px.
#  - LiberationSans-BoldItalic.ttf (OFL, github.com/liberationfonts): the Stock
#    style's bold italic numerals and labels, the factory cluster look.
# Without them the bitmap Montserrat stands in. Rerun `idf.py reconfigure`
# after adding or removing a file.
set(embed "")
set(have_ttf 0)
set(have_italic 0)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Montserrat-Light.ttf")
    list(APPEND embed "Montserrat-Light.ttf")
    set(have_ttf 1)
    message(STATUS "fonts: Montserrat-Light.ttf embedded")
else()
    message(STATUS "fonts: Montserrat-Light.ttf not found, digits capped at 48 px")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LiberationSans-BoldItalic.ttf")
    list(APPEND embed "LiberationSans-BoldItalic.ttf")
    set(have_italic 1)
    message(STATUS "fonts: LiberationSans-BoldItalic.ttf embedded")
else()
    message(STATUS "fonts: LiberationSans-BoldItalic.ttf not found, Stock numerals use bitmap Montserrat")
endif()
idf_component_register(SRCS "fonts.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES ${embed}
                       REQUIRES lvgl__lvgl)
target_compile_definitions(${COMPONENT_LIB} PUBLIC FONTS_HAVE_TTF=${have_ttf} FONTS_HAVE_ITALIC=${have_italic})
