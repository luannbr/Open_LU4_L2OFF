#include "HtmlBuilderPacketEx.h"

#include "DBPacketEx.h"
#include "HtmlBuilder.h"

#include "L2Core/src/inc/Guard.h"

class CDBSocket;

namespace {

#if defined(__clang__) || defined(__GNUC__)
#define L2_USED_INTERNAL_CALLBACK __attribute__((used))
#else
#define L2_USED_INTERNAL_CALLBACK
#endif

// Current L2Server 0x0078e154. The same normalized body also exists in
// DBPacketEx.obj at 0x0069dadc. HtmlBuilderPacketEx's later registration wins.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_MAIL_LIST_EX(CDBSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    ListMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e2a4. Earlier duplicate 0x0069dc2c.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_READ_MAIL_EX(CDBSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    ReadMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e404. Earlier duplicate 0x0069dd8c.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_SEND_MAIL_EX(CDBSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    SendMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e540. Earlier duplicate 0x0069dec8.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_SAVE_MAIL_EX(CDBSocket* const socket, const unsigned char* const packet) {
    L2_GUARD_FUNCTION();
    SaveMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e67c. Earlier duplicate 0x0069e004.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_GET_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifyTempMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e7dc. Earlier duplicate 0x0069e164.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_REPLY_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ReplyMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078e93c. Earlier duplicate 0x0069e2c4.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_FORWARD_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ForwardMailFormHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078ea9c. Earlier duplicate 0x0069e424.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_MODIFY_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifyTempMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078ebc8. Earlier duplicate 0x0069e550.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_MODIFY_SEND_TEMP_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ModifySendTempMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078ed08. Earlier duplicate 0x0069e690.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_DELETE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    DeleteMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current L2Server 0x0078ee5c. Earlier duplicate 0x0069e7e4.
L2_USED_INTERNAL_CALLBACK bool ReplyHtmlBuilder_RP_ARCHIVE_MAIL_EX(
    CDBSocket* const socket, const unsigned char* const packet
) {
    L2_GUARD_FUNCTION();
    ArchiveMailHtmlBuilder builder(socket, packet);
    builder.BuildHtml();
    return false;
}

// Current CRT order places these eleven HtmlBuilderPacketEx.cpp owners after
// the complete DBPacketEx.cpp registration stream, so they are the effective
// callbacks for the duplicated mail opcodes.
L2_BIND_PACKET(kBindRpExHtml002, g_DBPacketTableEx, 0x002, &ReplyHtmlBuilder_RP_MAIL_LIST_EX);
L2_BIND_PACKET(kBindRpExHtml003, g_DBPacketTableEx, 0x003, &ReplyHtmlBuilder_RP_READ_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml005, g_DBPacketTableEx, 0x005, &ReplyHtmlBuilder_RP_SEND_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml006, g_DBPacketTableEx, 0x006, &ReplyHtmlBuilder_RP_SAVE_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml008, g_DBPacketTableEx, 0x008, &ReplyHtmlBuilder_RP_GET_TEMP_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml009, g_DBPacketTableEx, 0x009, &ReplyHtmlBuilder_RP_REPLY_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml00A, g_DBPacketTableEx, 0x00a, &ReplyHtmlBuilder_RP_FORWARD_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml00B, g_DBPacketTableEx, 0x00b, &ReplyHtmlBuilder_RP_MODIFY_TEMP_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml00C, g_DBPacketTableEx, 0x00c, &ReplyHtmlBuilder_RP_MODIFY_SEND_TEMP_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml00D, g_DBPacketTableEx, 0x00d, &ReplyHtmlBuilder_RP_DELETE_MAIL_EX);
L2_BIND_PACKET(kBindRpExHtml00E, g_DBPacketTableEx, 0x00e, &ReplyHtmlBuilder_RP_ARCHIVE_MAIL_EX);

#undef L2_USED_INTERNAL_CALLBACK

} // namespace
