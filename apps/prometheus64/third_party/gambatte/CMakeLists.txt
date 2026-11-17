# The Game Boy and Game Boy Color emulator core: Gambatte for libretro (GNU GPL version 2), at a
# pinned commit. Built here from its source files (the list in its Makefile.common) into
# gambatte_libretro.so / .dll next to the app, like the GBA core (third_party/gpsp).
#
# Gambatte is chosen for its accuracy (it passes the hardware test ROMs that matter for commercial
# games), the Game Boy Color's colours and real-time clock (Pokemon Gold, Silver and Crystal), and
# a link cable built on two calls a frontend can carry (net_serial.h).
#
# Changes (each made with a check that stops the build if Gambatte's text has changed):
# - The link cable runs over the app's own link (the libretro netpacket interface: the relay, or
#   two games on one computer) instead of Gambatte's direct network connection: net_serial.cpp is
#   replaced by this folder's, and three small hooks are added.
# - A transfer started by this console (its own clock) lasts until the other console's byte
#   arrives, instead of stopping the whole emulator while it waits: the game keeps running, waiting
#   for the transfer to end as it does on the console (gambatte-memory.cpp, updateSerial).
# - The incoming byte goes into the serial register whole at the end of a transfer: shifted in bit
#   by bit, it came out garbled whenever the game read the registers mid-transfer.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(gambatte_libretro LANGUAGES C CXX)
endif()
include(FetchContent)

set(P64_GAMBATTE_REPO https://github.com/libretro/gambatte-libretro.git)
set(P64_GAMBATTE_COMMIT 77f2567d718b8973d02caa2daa8dce4953faf811)
FetchContent_Declare(p64_gambatte GIT_REPOSITORY ${P64_GAMBATTE_REPO} GIT_TAG ${P64_GAMBATTE_COMMIT})
FetchContent_GetProperties(p64_gambatte)
if(NOT p64_gambatte_POPULATED)
  FetchContent_Populate(p64_gambatte)  # sources only: Gambatte's libretro build is a Makefile
endif()
set(G "${p64_gambatte_SOURCE_DIR}/libgambatte")
set(C "${G}/libretro-common")
set(P "${CMAKE_CURRENT_BINARY_DIR}/patched")

# Copies `file` (relative to libgambatte) into the patched folder with `old` replaced by `new`.
function(p64_patch file old new)
  if(EXISTS "${P}/${file}")
    file(READ "${P}/${file}" text)
  else()
    file(READ "${G}/${file}" text)
  endif()
  string(FIND "${text}" "${old}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "Gambatte's ${file} has changed: re-check the change to it in ${CMAKE_CURRENT_LIST_FILE}")
  endif()
  string(REPLACE "${old}" "${new}" text "${text}")
  file(WRITE "${P}/${file}" "${text}")
endfunction()
file(REMOVE_RECURSE "${P}")

# The cable's interface: the transfer waiting for its answer, and the serial registers as the game
# leaves them (a slave answers with its byte when the other console clocks one).
p64_patch(src/serial_io.h "		virtual unsigned char send(unsigned char data, bool fastCgb) = 0;"
"		virtual unsigned char send(unsigned char data, bool fastCgb) = 0;
		// Project Prometheus: a transfer this console clocked, waiting for the other's byte.
		virtual bool waiting() { return false; }
		virtual bool reply(unsigned char& in) { return false; }
		virtual void registers(unsigned char sb, unsigned char sc) {}")

p64_patch(src/gambatte-memory.cpp "	// Periodically checks if serial data is received
	if ((serial_io_ != 0) &&"
"	// Periodically checks if serial data is received
	if (serial_io_ != 0)
		serial_io_->registers(ioamhram_[0x101], ioamhram_[0x102]);
	if ((serial_io_ != 0) &&")
p64_patch(src/gambatte-memory.cpp "		if (intreq_.eventTime(intevent_serial) <= cc) {
#ifdef HAVE_NETWORK
			bool fire = ((ioamhram_[0x102] & 0x80) == 0x80);"
"		if (intreq_.eventTime(intevent_serial) <= cc) {
#ifdef HAVE_NETWORK
			if (serial_io_ != 0 && serial_io_->waiting()) {
				unsigned char answer;
				if (!serial_io_->reply(answer)) {
					// Project Prometheus: the other console's byte is still on its way. The
					// transfer lasts until it arrives; the game waits for its end, as it does
					// on the console.
					intreq_.setEventTime<intevent_serial>(cc + 0x800);
					return;
				}
				serialize_value_ = answer;
			}
			bool fire = ((ioamhram_[0x102] & 0x80) == 0x80);")
p64_patch(src/gambatte-memory.cpp "		} else {
			int const targetCnt = serialCntFrom(intreq_.eventTime(intevent_serial) - cc,"
"		} else if (serial_io_ == 0) {
			// Project Prometheus: with a cable, the byte comes in whole when the transfer ends. Shifted
			// in bit by bit here, a game reading the registers mid-transfer (as one waiting for its
			// end does) got the incoming byte's top bits over and over: the other console's byte
			// arrived garbled (Gambatte's own network link had the same fault).
			int const targetCnt = serialCntFrom(intreq_.eventTime(intevent_serial) - cc,")

# The frontend's link: offered at start-up, and the cable always plugged into it (it does nothing
# until a link starts).
p64_patch(libretro/libretro.cpp "   gb.setInputGetter(&gb_input);
#ifdef DUAL_MODE"
"   gb.setInputGetter(&gb_input);
#ifdef HAVE_NETWORK
   p64_link_register(environ_cb);  /* Project Prometheus */
#endif
#ifdef DUAL_MODE")
p64_patch(libretro/libretro.cpp "      default:
         gb_net_serial.stop();
         gb.setSerialIO(NULL);
         break;"
"      default:
         gb.setSerialIO(&gb_net_serial);  /* Project Prometheus: the frontend's link */
         break;")

set(S
  bootloader.cpp cpu.cpp gambatte.cpp initstate.cpp interrupter.cpp interruptrequester.cpp
  gambatte-memory.cpp sound.cpp statesaver.cpp tima.cpp video.cpp video_libretro.cpp mem/cartridge.cpp
  mem/cartridge_libretro.cpp mem/huc3.cpp mem/memptrs.cpp mem/rtc.cpp sound/channel1.cpp sound/channel2.cpp
  sound/channel3.cpp sound/channel4.cpp sound/duty_unit.cpp sound/envelope_unit.cpp sound/length_counter.cpp
  video/ly_counter.cpp video/lyc_irq.cpp video/next_m0_time.cpp video/ppu.cpp video/sprite_mapper.cpp)
set(SOURCES)
foreach(f ${S})
  if(EXISTS "${P}/src/${f}")
    list(APPEND SOURCES "${P}/src/${f}")
  else()
    list(APPEND SOURCES "${G}/src/${f}")
  endif()
endforeach()
add_library(gambatte_libretro SHARED ${SOURCES}
  "${P}/libretro/libretro.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/net_serial.cpp"
  ${G}/libretro/gambatte_log.c ${G}/libretro/blipper.c ${G}/libretro/cc_resampler.c
  ${C}/compat/compat_posix_string.c ${C}/compat/compat_snprintf.c ${C}/compat/compat_strcasestr.c
  ${C}/compat/compat_strl.c ${C}/compat/fopen_utf8.c ${C}/encodings/encoding_utf.c ${C}/file/file_path.c
  ${C}/file/file_path_io.c ${C}/streams/file_stream.c ${C}/streams/file_stream_transforms.c
  ${C}/string/stdstring.c ${C}/time/rtime.c ${C}/vfs/vfs_implementation.c)
set_target_properties(gambatte_libretro PROPERTIES PREFIX "")
# The patched copies first, so their includes ("serial_io.h") find each other; this folder's
# net_serial.h before Gambatte's own, and the app's libretro.h (it has the netpacket interface).
target_include_directories(gambatte_libretro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../libretro" "${P}/src" ${G}/src ${G}/include
  ${p64_gambatte_SOURCE_DIR}/common ${p64_gambatte_SOURCE_DIR}/common/resample ${G}/libretro ${C}/include)
target_compile_definitions(gambatte_libretro PRIVATE __LIBRETRO__ HAVE_STDINT_H HAVE_INTTYPES_H
  CC_RESAMPLER_NO_HIGHPASS HAVE_NETWORK VIDEO_RGB565 NDEBUG "GIT_VERSION=\" ${P64_GAMBATTE_COMMIT}\"")
if(WIN32)
  target_link_options(gambatte_libretro PRIVATE -static-libgcc -static-libstdc++ -static -s)
endif()
target_compile_options(gambatte_libretro PRIVATE -O2 -w)
target_link_options(gambatte_libretro PRIVATE "-Wl,--version-script=${G}/libretro/link.T")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/GB-CORE-SOURCE.txt"
"Game Boy and Game Boy Color emulator core: Gambatte (libretro), GNU General Public License version 2.
Source: ${P64_GAMBATTE_REPO}
Commit: ${P64_GAMBATTE_COMMIT}
Changed: the link cable runs over Project Prometheus's link (net_serial.cpp replaced, small hooks in
serial_io.h, gambatte-memory.cpp and libretro.cpp); the changes are in
apps/prometheus64/third_party/gambatte of Project Prometheus's source.
")
