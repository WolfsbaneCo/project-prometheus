// Project Prometheus: the Game Boy link cable over the frontend's link (the libretro netpacket
// interface), replacing Gambatte's own direct network link (net_serial.cpp). See CMakeLists.txt.
#ifndef P64_NET_SERIAL_H
#define P64_NET_SERIAL_H

#include <gambatte.h>
#include <string>

#include "libretro.h"

class NetSerial : public gambatte::SerialIO {
 public:
  NetSerial();
  ~NetSerial();

  // Gambatte's own direct network link: not built. The link starts when the frontend starts it.
  bool start(bool, int, const std::string&) { return false; }
  void stop() {}

  virtual bool check(unsigned char out, unsigned char& in, bool& fastCgb);
  virtual unsigned char send(unsigned char data, bool fastCgb);
  virtual bool waiting();
  virtual bool reply(unsigned char& in);
  virtual void registers(unsigned char sb, unsigned char sc);
};

// Offers the link to the frontend (RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE).
void p64_link_register(retro_environment_t environ_cb);

#endif
