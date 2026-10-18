#pragma once

// The current callbacks in HtmlBuilderPacketEx.cpp have internal linkage.
// This physical header is kept because it is present in the pinned
// L2Server project manifest. The shared ReplyExPacket table lifecycle remains
// a separate unresolved owner.
