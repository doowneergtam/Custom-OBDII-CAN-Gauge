# Large-digit font. Drop Montserrat-Light.ttf (OFL, from Google Fonts) into
# this folder to enable TrueType digits above 48 px; without it the gauge
# keeps the 48 px bitmap font.
set(ttf "${CMAKE_CURRENT_SOURCE_DIR}/Montserrat-Light.ttf")
if(EXISTS "${ttf}")
    idf_component_register(SRCS "fonts.c"
                           INCLUDE_DIRS "include"
                           EMBED_FILES "Montserrat-Light.ttf"
                           REQUIRES lvgl__lvgl)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC FONTS_HAVE_TTF=1)
    message(STATUS "fonts: Montserrat-Light.ttf embedded")
else()
    idf_component_register(SRCS "fonts.c"
                           INCLUDE_DIRS "include"
                           REQUIRES lvgl__lvgl)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC FONTS_HAVE_TTF=0)
    message(STATUS "fonts: Montserrat-Light.ttf not found, digits capped at 48 px")
endif()
