#pragma once
// The Gummi flight in co-op (TODO 5.2 stage 1 items 5-6, see gummi_flight.cpp).

namespace kh2coop {

void GummiFlightInit();   // from AvatarLinkInit
void GummiFlightFrame();  // from OnFrame (game thread)
void GummiFlightOnPacket(const char* buf, int n);  // "KH2f"
bool GummiFlightFlying();                          // our ship is flying (the flight's pause can open)

}  // namespace kh2coop
