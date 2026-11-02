# Local SDL 3.2.28 compatibility patch. Keep upstream copyright/license intact.
# Source/rationale and validation are recorded in third_party/sdl3/README.md.
set(wiiz_switch_source "${sdl3_SOURCE_DIR}/src/joystick/hidapi/SDL_hidapi_switch.c")
file(READ "${wiiz_switch_source}" wiiz_switch_text)
set(wiiz_switch_old [=[                    if (SDL_memcmp(&reply->spiReadData.opData, pBuf, ucLen) != 0) {]=])
set(wiiz_switch_new [=[                    /* Wiiz: accept a longer SPI reply only when its data is present. */
                    const SwitchSPIOpData_t *requested = (const SwitchSPIOpData_t *)pBuf;
                    const size_t data_offset = 1 + offsetof(SwitchSubcommandInputPacket_t, spiReadData.rgucReadData);
                    const size_t available = nRead > data_offset ? SDL_min((size_t)nRead - data_offset, sizeof(reply->spiReadData.rgucReadData)) : 0;
                    if (!wiizSwitchSPIReplyMatches(requested->unAddress, requested->ucLength,
                                                   reply->spiReadData.opData.unAddress, reply->spiReadData.opData.ucLength, available)) {]=])
string(FIND "${wiiz_switch_text}" "wiizSwitchSPIReplyMatches" wiiz_switch_already_patched)
if(wiiz_switch_already_patched EQUAL -1)
  string(FIND "${wiiz_switch_text}" "${wiiz_switch_old}" wiiz_switch_found)
  if(wiiz_switch_found EQUAL -1)
    message(FATAL_ERROR "SDL Switch SPI patch does not match this SDL source")
  endif()
  string(REPLACE "${wiiz_switch_old}" "${wiiz_switch_new}" wiiz_switch_text "${wiiz_switch_text}")
  string(REPLACE "#include \"SDL_hidapijoystick_c.h\"" "#include \"SDL_hidapijoystick_c.h\"\n#include \"wiiz_spi_reply.h\"" wiiz_switch_text "${wiiz_switch_text}")
endif()
string(FIND "${wiiz_switch_text}" "m_bWiizBspUserCalibration" wiiz_switch_face_patched)
if(wiiz_switch_face_patched EQUAL -1)
  string(REPLACE "    bool m_bUseButtonLabels;" "    bool m_bUseButtonLabels;\n    bool m_bWiizBspUserCalibration; /* Wiiz: BSP-D3 reply signature */" wiiz_switch_text "${wiiz_switch_text}")
  set(wiiz_face_old [=[                    const size_t data_offset = 1 + offsetof(SwitchSubcommandInputPacket_t, spiReadData.rgucReadData);]=])
  set(wiiz_face_new [=[                    /* Wiiz: this BSP-D3 reply pair reports labeled face buttons.
                     * Reuse SDL's existing RemapButton; do not change genuine Pro/Xbox maps. */
                    if (requested->unAddress == 0x8010 && requested->ucLength == 22 &&
                        reply->spiReadData.opData.unAddress == 0x8010 && reply->spiReadData.opData.ucLength == 24) {
                        ctx->m_bWiizBspUserCalibration = true;
                    }
                    if (ctx->m_bWiizBspUserCalibration && requested->unAddress == 0x603d && requested->ucLength == 18 &&
                        reply->spiReadData.opData.unAddress == 0x603d && reply->spiReadData.opData.ucLength == 25) {
                        ctx->m_bUseButtonLabels = true;
                    }
                    const size_t data_offset = 1 + offsetof(SwitchSubcommandInputPacket_t, spiReadData.rgucReadData);]=])
  string(REPLACE "${wiiz_face_old}" "${wiiz_face_new}" wiiz_switch_text "${wiiz_switch_text}")
endif()
file(READ "${wiiz_switch_source}" wiiz_switch_current)
if(NOT wiiz_switch_current STREQUAL wiiz_switch_text)
  file(WRITE "${wiiz_switch_source}" "${wiiz_switch_text}")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/../third_party/sdl3/wiiz_spi_reply.h"
               "${sdl3_SOURCE_DIR}/src/joystick/hidapi/wiiz_spi_reply.h" COPYONLY)
